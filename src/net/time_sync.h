#pragma once

#include <stdint.h>

#include "../store/config_store.h"

namespace time_sync {

// Read the PCF8563 RTC into the system clock and apply cfg.tz.
// Call once per wake, before anything needs wall time.
void initFromRtc(const Config& cfg);

// SNTP sync (blocking, a few seconds). On success writes the RTC back.
bool syncSntp(const Config& cfg);

bool timeValid();      // system clock looks like real wall time
uint32_t nowEpoch();   // unix seconds

}  // namespace time_sync
