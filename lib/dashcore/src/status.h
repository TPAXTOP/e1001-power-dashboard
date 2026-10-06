// Firmware-only display logic: device battery, indoor/precipitation helpers
// and the status bar's outage countdown. Unlike derive.h this has no web app
// counterpart, so there is no parity constraint. Pure C++, host-unit-tested.
#pragma once

#include <stdint.h>

#include "data_model.h"

namespace dash {

// --- device battery ----------------------------------------------------------

// Resting single-cell LiPo voltage -> 0..100 % (piecewise-linear curve).
// Returns -1 for volts <= 0 ("unknown" from power_mgmt::batteryVolts()).
int batteryPercentFromVolts(float volts);

// Below this much time since the last full charge, a drain rate is noise.
constexpr uint32_t kDrainMinS = 12 * 3600;

// Average drain since the last full charge, in % per 24 h (rounded).
// Returns -1 when sinceFullS < kDrainMinS or percentNow is unknown (< 0).
int drainPerDay(uint32_t sinceFullS, int percentNow);

// Compact duration: "45m", "1h 20m", "2h", "14h", "3d 4h", "3d", "12d". buf >= 12.
void formatDuration(uint32_t seconds, char* buf, int bufLen);

// --- weather -------------------------------------------------------------------

// Highest precipitation probability over `hours` hourly entries, starting at
// the hour containing nowLocalIso ("YYYY-MM-DDTHH:MM", Kyiv; "" = from the
// first entry). Returns -1 when no entry in that window has a probability.
int maxPrecipProb(const WeatherData& w, const char* nowLocalIso, int hours);

// --- outage countdown ------------------------------------------------------------

// One status-bar line for the outage schedule at nowMin (minutes since local
// midnight; the schedule must already be rolled over to today):
//   "Emergency outages, schedule suspended"
//   "Power off now · back at 17:30"
//   "Next outage 14:00-17:30 · in 1h 20m"
//   "Outage tomorrow 08:00-11:30"
// Only days whose status is "ScheduleApplies" count. Returns false when there
// is nothing to say. buf >= 64.
bool formatOutageStatus(const OutageSchedule& s, int nowMin, char* buf, int bufLen);

}  // namespace dash
