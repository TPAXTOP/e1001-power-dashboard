#include "alert_api.h"

#include <ArduinoJson.h>
#include <alerts.h>
#include <string.h>

#include "../../include/defaults.h"
#include "../net/https.h"
#include "../util/log.h"

namespace alert_api {

static const int kMaxRegions = 3;
static const int kMaxEntries = 12;
// Refetch the regions at least this often even when lastActionIndex says
// nothing changed (in case an update ever slips past the index).
static const uint32_t kFullRefetchS = 900;

static bool authFailed(int code) { return code == 401 || code == 403; }

// Response (AlertRegionModel[]): [{regionId, ..., activeAlerts: [{regionId,
// type, lastUpdate, activeAlertLevels: [{alertLevel, reason, createdAt}]}]}].
// activeAlerts and activeAlertLevels are nullable.
static int collect(JsonArrayConst regions, dash::AlertEntry* out, int n) {
  for (JsonObjectConst region : regions) {
    for (JsonObjectConst alert : region["activeAlerts"].as<JsonArrayConst>()) {
      dash::AlertType type = dash::alertTypeFromString(alert["type"] | "");
      JsonArrayConst levels = alert["activeAlertLevels"].as<JsonArrayConst>();
      if (levels.isNull() || levels.size() == 0) {
        // Listed without levels: still an alert (Red), since its last update.
        if (n >= kMaxEntries) return n;
        dash::AlertEntry& e = out[n++];
        memset(&e, 0, sizeof(e));
        e.level = dash::ALERT_RED;
        e.type = type;
        dash::parseIsoUtc(alert["lastUpdate"] | "", e.createdEpoch);
        continue;
      }
      for (JsonObjectConst lvl : levels) {
        if (n >= kMaxEntries) return n;
        dash::AlertEntry& e = out[n++];
        memset(&e, 0, sizeof(e));
        e.level = dash::alertLevelFromString(lvl["alertLevel"] | "");
        e.type = type;
        dash::copyUtf8(e.reason, lvl["reason"] | "", sizeof(e.reason));
        dash::parseIsoUtc(lvl["createdAt"] | "", e.createdEpoch);
      }
    }
  }
  return n;
}

// lastActionIndex, or -1 when the status request failed.
static int64_t actionIndex(const Config& cfg, int& code) {
  JsonDocument doc;
  String url = String(DEF_ALERT_API_URL) + "/api/v3/alerts/status";
  if (!net::httpGetJson(url, doc, nullptr, &code, cfg.alertApiKey.c_str())) return -1;
  if (!doc["lastActionIndex"].is<int64_t>()) {
    LOGE("alert", "status: no lastActionIndex");
    return -1;
  }
  return doc["lastActionIndex"].as<int64_t>();
}

bool fetch(const Config& cfg, const AlertCache* prev, uint32_t now, AlertCache& out,
           FetchError* error) {
  if (error) *error = ERR_REQUEST;
  if (!cfg.alertApiKey.length()) {
    if (error) *error = ERR_AUTH;
    return false;
  }

  int code = 0;
  int64_t index = actionIndex(cfg, code);
  if (authFailed(code)) {
    if (error) *error = ERR_AUTH;
    return false;
  }
  if (prev && index >= 0 && prev->actionIndex == index &&
      strcmp(prev->regions, cfg.alertRegions.c_str()) == 0 && now >= prev->fetchedEpoch &&
      now - prev->fetchedEpoch < kFullRefetchS) {
    out = *prev;
    if (error) *error = ERR_NONE;
    LOGI("alert", "unchanged (action %lld), level %u", (long long)index, out.status.level);
    return true;
  }

  JsonDocument filter;
  JsonObject a = filter[0]["activeAlerts"][0].to<JsonObject>();
  a["type"] = true;
  a["lastUpdate"] = true;
  JsonObject l = a["activeAlertLevels"][0].to<JsonObject>();
  l["alertLevel"] = true;
  l["reason"] = true;
  l["createdAt"] = true;

  static dash::AlertEntry entries[kMaxEntries];  // ~1.3 KB, off the stack
  int n = 0, regions = 0;
  String list = cfg.alertRegions;
  int from = 0;
  while (from <= (int)list.length() && regions < kMaxRegions) {
    int comma = list.indexOf(',', from);
    if (comma < 0) comma = list.length();
    String id = list.substring(from, comma);
    id.trim();
    from = comma + 1;
    if (!id.length()) continue;
    regions++;

    JsonDocument doc;
    String url = String(DEF_ALERT_API_URL) + "/api/v3/alerts/" + id;
    if (!net::httpGetJson(url, doc, &filter, &code, cfg.alertApiKey.c_str())) {
      if (error && authFailed(code)) *error = ERR_AUTH;
      return false;
    }
    // Anything but the documented array would parse as "no alerts", i.e. a
    // false all-clear: treat it as a failure instead.
    if (!doc.is<JsonArrayConst>()) {
      LOGE("alert", "region %s: unexpected response shape", id.c_str());
      return false;
    }
    n = collect(doc.as<JsonArrayConst>(), entries, n);
  }
  if (!regions) return false;

  memset(&out, 0, sizeof(out));
  out.status = dash::pickAlert(entries, n);
  out.actionIndex = index;
  out.fetchedEpoch = now;
  strlcpy(out.regions, cfg.alertRegions.c_str(), sizeof(out.regions));
  if (error) *error = ERR_NONE;
  LOGI("alert", "%d region(s), %d active level(s) -> level %u type %u (action %lld)", regions, n,
       out.status.level, out.status.type, (long long)index);
  return true;
}

}  // namespace alert_api
