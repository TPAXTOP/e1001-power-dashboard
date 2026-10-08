// Air raid alerts (api.ukrainealarm.com v3): picking the one alert to show
// and its status-bar text. Firmware-only (the web app has no alerts). Pure
// C++, host-unit-tested; the HTTP/JSON side lives in src/api/alert_api.cpp.
#pragma once

#include <stdint.h>

#include "data_model.h"

namespace dash {

// One active alert level of one alert type, as listed by the API.
struct AlertEntry {
  uint8_t level;          // AlertLevel (never ALERT_NONE)
  uint8_t type;           // AlertType
  uint32_t createdEpoch;  // 0 = unknown
  char reason[kAlertReasonMax];
};

// "AIR", "ARTILLERY", "URBAN_FIGHTS", "CHEMICAL", "NUCLEAR"; anything else
// ("UNKNOWN", "INFO", "CUSTOM", newer types) is ALERT_OTHER.
AlertType alertTypeFromString(const char* s);
// "Red" / "Yellow" (any case). Anything else, including a missing level (an
// alert listed without levels), counts as Red: an alert must never vanish.
AlertLevel alertLevelFromString(const char* s);

// Copies at most size-1 bytes of UTF-8 without splitting a character, with
// surrounding whitespace removed. size >= 1.
void copyUtf8(char* dst, const char* src, int size);

// "2026-10-08T05:29:56.21972Z" (any fraction digits, optional Z) -> unix
// seconds, UTC. False for anything unparseable.
bool parseIsoUtc(const char* s, uint32_t& epoch);

// The alert to show: Red beats Yellow, an air alert beats other types at the
// same level, then the earliest. extraTypes counts the other active types.
// No entries -> level ALERT_NONE.
AlertStatus pickAlert(const AlertEntry* entries, int n);

// Ukrainian status-bar text: the API's reason when it sent one, else a fixed
// name for the type ("ПОВІТРЯНА ТРИВОГА" for Red, sentence case for Yellow);
// then " з 14:05" (sinceText = local start time, "" = leave out) and " +1"
// for further active types. Only characters the Cyrillic status-bar fonts
// have. buf >= 128.
void formatAlert(const AlertStatus& a, const char* sinceText, char* buf, int bufLen);

}  // namespace dash
