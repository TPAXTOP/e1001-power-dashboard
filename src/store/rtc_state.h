// Fast-changing wake state kept in RTC RAM across deep sleep. Unlike
// state_store (NVS), it is lost on power loss or a brownout, which only costs
// a retry or a full refresh. Offline wakes write nothing else, so the flash
// sees no wear from waking every few minutes.
#pragma once

#include <stdint.h>

#include "state_store.h"

struct RtcState {
  uint32_t lastAttemptEpoch[SRC_COUNT];  // last fetch attempt (success or not)
  // Values currently on screen, for hysteresis (see dash::stickyRound).
  bool hasShownIndoor;
  int16_t shownTemp;
  int16_t shownRh;
  int16_t shownBattery;  // 5 % step, -1 = none yet
  uint8_t connectivity;  // dash::Connectivity after the last online wake
  uint8_t outageErr;     // why the last outage fetch failed (yasno_api::FetchError)
  uint8_t alertErr;      // why the last alert fetch failed (alert_api::FetchError)
  uint8_t alertAuthFails;  // consecutive 401/403 answers (the API also rate-limits with 401)
  uint32_t lastAlertRequestEpoch;  // wall clock of the last alert request (SD log)
  // WiFi fast reconnect: the AP and channel of the last successful join.
  bool hasBssid;
  uint8_t bssid[6];
  uint8_t channel;
};

namespace rtc_state {

// The state of this boot; zeroed (shownBattery -1) when RTC RAM was invalid.
RtcState& get();

// Seal it for the next wake (call right before deep sleep or restart).
void commit();

}  // namespace rtc_state
