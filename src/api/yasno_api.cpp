#include "yasno_api.h"

#include <ArduinoJson.h>

#include "../net/https.h"
#include "../util/log.h"

namespace yasno_api {

static void parseDay(JsonObjectConst day, dash::OutageDay& out) {
  memset(&out, 0, sizeof(out));
  const char* date = day["date"];
  if (day.isNull() || !date || !*date) {
    out.present = false;
    return;
  }
  out.present = true;
  strlcpy(out.date, date, sizeof(out.date));
  strlcpy(out.status, day["status"] | "Unknown", sizeof(out.status));

  int n = 0;
  for (JsonObjectConst slot : day["slots"].as<JsonArrayConst>()) {
    if (n >= dash::kSlotsMax) break;
    if (!slot["start"].is<int>() || !slot["end"].is<int>() || !slot["type"].is<const char*>())
      continue;
    dash::OutageSlot& s = out.slots[n];
    s.startMin = slot["start"].as<int>();
    s.endMin = slot["end"].as<int>();
    s.type = strcmp(slot["type"].as<const char*>(), "Definite") == 0 ? dash::SLOT_DEFINITE
                                                                     : dash::SLOT_NOT_PLANNED;
    n++;
  }
  out.slotCount = n;
}

bool fetch(const Config& cfg, dash::OutageSchedule& out, FetchError* error) {
  if (error) *error = ERR_REQUEST;
  JsonDocument filter;
  filter[cfg.yasnoGroup] = true;  // whole subtree of our group only

  JsonDocument doc;
  if (!net::httpGetJson(cfg.yasnoUrl, doc, &filter)) return false;

  JsonObjectConst group = doc[cfg.yasnoGroup];
  if (group.isNull()) {
    // Yasno renumbers groups from time to time (Kyiv went from 1.1-6.2 to
    // 1.1-60.1); a stale group must show up as an error, not as "no outages".
    LOGE("yasno", "group %s not in response", cfg.yasnoGroup.c_str());
    if (error) *error = ERR_GROUP_MISSING;
    return false;
  }

  memset(&out, 0, sizeof(out));
  parseDay(group["today"], out.today);
  parseDay(group["tomorrow"], out.tomorrow);
  strlcpy(out.groupId, cfg.yasnoGroup.c_str(), sizeof(out.groupId));
  strlcpy(out.updatedOn, group["updatedOn"] | "", sizeof(out.updatedOn));
  if (error) *error = ERR_NONE;

  LOGI("yasno", "ok: today[%s %s %d slots] tomorrow[%s %s %d slots]",
       out.today.present ? out.today.date : "-", out.today.status, out.today.slotCount,
       out.tomorrow.present ? out.tomorrow.date : "-", out.tomorrow.status,
       out.tomorrow.slotCount);
  return true;
}

}  // namespace yasno_api
