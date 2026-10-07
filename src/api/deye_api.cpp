#include "deye_api.h"

#include <ArduinoJson.h>
#include <derive.h>
#include <status.h>

#include <vector>

#include "../net/https.h"
#include "../net/time_sync.h"
#include "../util/log.h"

namespace deye_api {

// Measure point key variants across inverter models (matched lowercase,
// substring) - ported from the web app's MEASURE_POINT_KEYS.
static const char* kBatteryPowerKeys[] = {"batterypower", "battppower", "battery_power",
                                          "batt_power"};
static const char* kLoadPowerKeys[] = {"loadpower", "totalloadpower", "load_power",
                                       "total_load_power", "usepower"};

static bool keyMatches(const char* key, const char* const* candidates, int count) {
  if (!key) return false;
  String lower(key);
  lower.toLowerCase();
  for (int i = 0; i < count; i++) {
    if (lower.indexOf(candidates[i]) >= 0) return true;
  }
  return false;
}

// Deye returns most numbers as JSON strings ("95", "1728248400") but not
// consistently; accept both like the web app's parseFloat()/`* 1000` does.
static bool numberFrom(JsonVariantConst v, double& out) {
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    char* end = nullptr;
    out = strtod(s, &end);
    return end != s;
  }
  if (v.is<double>()) {
    out = v.as<double>();
    return true;
  }
  return false;
}

static bool apiSuccess(const JsonDocument& doc) {
  return (doc["success"] | false) && strcmp(doc["code"] | "", "1000000") == 0;
}

static bool authenticate(const Config& cfg, PersistedState& st) {
  String url = cfg.deyeApiUrl + "/v1.0/account/token?appId=" + cfg.deyeAppId;

  JsonDocument body;
  body["appSecret"] = cfg.deyeAppSecret;
  body["email"] = cfg.deyeEmail;
  body["password"] = cfg.deyePasswordSha256;
  String payload;
  serializeJson(body, payload);

  JsonDocument doc;
  if (!net::httpPostJson(url, payload, doc)) return false;
  if (!apiSuccess(doc)) {
    LOGE("deye", "auth rejected: code=%s msg=%s", doc["code"] | "?", doc["msg"] | "?");
    return false;
  }

  // Token may be at top level or under data.
  const char* token = doc["accessToken"];
  if (!token || !*token) token = doc["data"]["accessToken"];
  long expiresIn = doc["expiresIn"] | 0L;
  if (expiresIn <= 0) expiresIn = doc["data"]["expiresIn"] | 3600L;
  if (!token || !*token) {
    LOGE("deye", "no access token in response");
    return false;
  }

  st.deyeToken = token;
  st.deyeTokenExpEpoch = time_sync::nowEpoch() + (uint32_t)expiresIn;
  LOGI("deye", "authenticated, token valid %lds", expiresIn);
  return true;
}

static bool ensureToken(const Config& cfg, PersistedState& st) {
  if (st.deyeToken.length() && st.deyeTokenExpEpoch > time_sync::nowEpoch() + 300) {
    return true;
  }
  return authenticate(cfg, st);
}

static bool fetchLatest(const Config& cfg, PersistedState& st, dash::BackupData& out) {
  JsonDocument body;
  body["deviceList"].add(cfg.deyeDeviceSn);
  String payload;
  serializeJson(body, payload);

  JsonDocument doc;
  if (!net::httpPostJson(cfg.deyeApiUrl + "/v1.0/device/latest", payload, doc, st.deyeToken))
    return false;
  if (!apiSuccess(doc)) {
    LOGE("deye", "latest rejected: code=%s msg=%s", doc["code"] | "?", doc["msg"] | "?");
    return false;
  }

  JsonObjectConst device = doc["deviceDataList"][0];
  if (device.isNull()) {
    LOGE("deye", "no device data");
    return false;
  }

  out.batteryPercent = 0;
  out.gridConnected = false;
  out.hasBatteryPower = false;
  out.hasLoadPower = false;

  for (JsonObjectConst item : device["dataList"].as<JsonArrayConst>()) {
    const char* key = item["key"];
    double value;
    if (!key || !numberFrom(item["value"], value)) continue;
    String lower(key);
    lower.toLowerCase();

    if (lower == "soc") {
      out.batteryPercent = value;
    } else if (lower.indexOf("gridvoltage") >= 0) {
      if (value > 100.0) out.gridConnected = true;
    }
    if (!out.hasBatteryPower && keyMatches(key, kBatteryPowerKeys, 4)) {
      out.batteryPowerWatts = value;
      out.hasBatteryPower = true;
    }
    if (!out.hasLoadPower && keyMatches(key, kLoadPowerKeys, 5)) {
      out.loadPowerWatts = value;
      out.hasLoadPower = true;
    }
  }

  double collectionTime = 0;
  numberFrom(device["collectionTime"], collectionTime);
  out.lastUpdateEpoch = collectionTime > 0 ? (uint32_t)collectionTime : time_sync::nowEpoch();
  return true;
}

bool fetchSoc(const Config& cfg, PersistedState& st, dash::SocHistory& hist) {
  if (!cfg.hasDeye()) return false;
  if (!ensureToken(cfg, st)) return false;

  // Incremental: only the part after the cached graph (30 min overlap, in
  // case the last bucket was still filling), or the full 24 h on first use.
  uint32_t endTs = time_sync::nowEpoch();
  uint32_t startTs = endTs - 24 * 60 * 60;
  if (hist.count > 0) {
    uint32_t last = hist.points[hist.count - 1].epoch;
    if (last > startTs + 1800 && last <= endTs) startTs = last - 1800;
  }

  JsonDocument body;
  body["deviceSn"] = cfg.deyeDeviceSn;
  body["startTimestamp"] = startTs;
  body["endTimestamp"] = endTs;
  body["measurePoints"].add("SOC");
  String payload;
  serializeJson(body, payload);

  JsonDocument doc;
  if (!net::httpPostJson(cfg.deyeApiUrl + "/v1.0/device/historyRaw", payload, doc, st.deyeToken))
    return false;
  if (!apiSuccess(doc)) {
    LOGE("deye", "history rejected: code=%s msg=%s", doc["code"] | "?", doc["msg"] | "?");
    return false;
  }

  JsonArrayConst list = doc["dataList"].as<JsonArrayConst>();
  std::vector<dash::BatteryPoint> points;
  points.reserve(list.size());
  for (JsonObjectConst item : list) {
    double t;
    if (!numberFrom(item["time"], t) || t <= 0) continue;
    for (JsonObjectConst dp : item["itemList"].as<JsonArrayConst>()) {
      const char* key = dp["key"];
      double value;
      if (!key || !numberFrom(dp["value"], value)) continue;
      String lower(key);
      lower.toLowerCase();
      if (lower.indexOf("soc") >= 0) {
        points.push_back({(uint32_t)t, (float)value});
        break;  // first SOC value per timestamp only
      }
    }
  }

  if (points.empty() && list.size()) {
    // Diagnose unexpected payload shapes instead of silently drawing nothing.
    String sample;
    serializeJson(list[0], sample);
    if (sample.length() > 300) sample = sample.substring(0, 300) + "...";
    LOGW("deye", "history: no SOC points in %u entries (window %lu-%lu): %s",
         (unsigned)list.size(), (unsigned long)startTs, (unsigned long)endTs, sample.c_str());
  }

  int before = hist.count;
  dash::mergeSocHistory(hist, points.data(), (int)points.size(), endTs);
  LOGI("deye", "history: %u raw since %lu -> %d points (was %d)", (unsigned)points.size(),
       (unsigned long)startTs, hist.count, before);
  return true;
}

bool fetchStatus(const Config& cfg, PersistedState& st, dash::BackupData& out) {
  if (!cfg.hasDeye()) {
    LOGW("deye", "not configured");
    return false;
  }
  if (!ensureToken(cfg, st)) return false;

  memset(&out, 0, sizeof(out));
  bool ok = fetchLatest(cfg, st, out);
  if (!ok) {
    // Token may have been revoked server-side; re-auth once and retry.
    LOGW("deye", "latest failed, re-authenticating once");
    st.deyeToken = "";
    if (!authenticate(cfg, st)) return false;
    if (!fetchLatest(cfg, st, out)) return false;
  }

  out.chargingStatus =
      dash::chargingStatusFrom(out.hasBatteryPower, out.batteryPowerWatts);
  out.hasRuntime = false;
  if (out.chargingStatus == dash::CHARGE_DISCHARGING && out.hasBatteryPower) {
    int32_t rt = dash::runtimeMinutes((float)cfg.deyeBattWh, out.batteryPercent,
                                      out.batteryPowerWatts);
    if (rt >= 0) {
      out.estimatedRuntimeMin = rt;
      out.hasRuntime = true;
    }
  }

  LOGI("deye", "ok: soc=%.0f%% grid=%d battW=%.0f loadW=%.0f status=%d age=%lds",
       out.batteryPercent, out.gridConnected,
       out.hasBatteryPower ? out.batteryPowerWatts : -999.0f,
       out.hasLoadPower ? out.loadPowerWatts : -999.0f, out.chargingStatus,
       (long)(time_sync::nowEpoch() - out.lastUpdateEpoch));
  return true;
}

}  // namespace deye_api
