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

// Response (AlertRegionModel[]): [{regionId, ..., activeAlerts: [{regionId,
// type, lastUpdate, activeAlertLevels: [{alertLevel, reason, createdAt}]}]}].
// activeAlerts and activeAlertLevels are nullable. ids == nullptr takes every
// region in the response (the single-region endpoint).
static int collect(JsonArrayConst regions, const String* ids, int idCount,
                   dash::AlertEntry* out, int n) {
  for (JsonObjectConst region : regions) {
    if (ids) {
      const char* rid = region["regionId"] | "";
      bool wanted = false;
      for (int i = 0; i < idCount && !wanted; i++) wanted = ids[i] == rid;
      if (!wanted) continue;
    }
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

bool fetch(const Config& cfg, uint32_t now, AlertCache& out, FetchError* error) {
  if (error) *error = ERR_REQUEST;
  if (!cfg.alertApiKey.length()) {
    if (error) *error = ERR_AUTH;
    return false;
  }

  String ids[kMaxRegions];
  int idCount = 0;
  String list = cfg.alertRegions;
  int from = 0;
  while (from <= (int)list.length() && idCount < kMaxRegions) {
    int comma = list.indexOf(',', from);
    if (comma < 0) comma = list.length();
    String id = list.substring(from, comma);
    id.trim();
    from = comma + 1;
    if (id.length()) ids[idCount++] = id;
  }
  if (!idCount) return false;

  JsonDocument filter;
  JsonObject r = filter[0].to<JsonObject>();
  r["regionId"] = true;
  JsonObject a = r["activeAlerts"][0].to<JsonObject>();
  a["type"] = true;
  a["lastUpdate"] = true;
  JsonObject l = a["activeAlertLevels"][0].to<JsonObject>();
  l["alertLevel"] = true;
  l["reason"] = true;
  l["createdAt"] = true;

  String url = String(DEF_ALERT_API_URL) + "/api/v3/alerts";
  if (idCount == 1) url += "/" + ids[0];

  JsonDocument doc;
  int code = 0;
  if (!net::httpGetJson(url, doc, &filter, &code, cfg.alertApiKey.c_str())) {
    if (code == 401 || code == 403) {
      if (error) *error = ERR_AUTH;
      LOGW("alert", "HTTP %d: key rejected, or used again within a minute", code);
    }
    return false;
  }
  // Anything but the documented array would parse as "no alerts", i.e. a
  // false all-clear: treat it as a failure instead.
  if (!doc.is<JsonArrayConst>()) {
    LOGE("alert", "unexpected response shape");
    return false;
  }

  static dash::AlertEntry entries[kMaxEntries];  // ~1.3 KB, off the stack
  int n = collect(doc.as<JsonArrayConst>(), idCount == 1 ? nullptr : ids, idCount, entries, 0);

  memset(&out, 0, sizeof(out));
  out.status = dash::pickAlert(entries, n);
  out.fetchedEpoch = now;
  strlcpy(out.regions, cfg.alertRegions.c_str(), sizeof(out.regions));
  if (error) *error = ERR_NONE;
  LOGI("alert", "%d region(s), %d active level(s) -> level %u type %u", idCount, n,
       out.status.level, out.status.type);
  return true;
}

}  // namespace alert_api
