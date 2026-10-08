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

// Charge detection without a charger signal (there is no USB/charger status
// on this board), from the pre-WiFi battery reading of each wake:
// - a new charge starts when the reading is >= kChargeRiseV above the lowest
//   reading since the previous charge (discharge never raises it that much;
//   ADC noise is ~10 mV). There is deliberately no "high voltage = just
//   charged" shortcut: a rested full cell sits within the plateau below for
//   hours, which would show "charging" all that time.
// - while charging, every reading is a new peak (constant-current phase) or
//   sits at the peak (constant-voltage phase): within kChargePlateauV of it.
// - unplugged (or charge terminated), the cell relaxes tens of mV below the
//   peak within minutes and then only falls: charging has ended.
// endEpoch is the last wake that still counted as charging, so "now -
// endEpoch" is the wall-clock time since the last charge, off by at most
// the relaxation time - not by the hours a rested full cell stays > 4.15 V.
constexpr float kChargeRiseV = 0.05f;
constexpr float kChargePlateauV = 0.015f;

struct ChargeState {
  float peakV = 0;        // highest reading of the current/last charge; 0 = none seen
  float minV = 0;         // lowest reading since that charge started; 0 = none
  uint32_t endEpoch = 0;  // last wake that counted as charging; 0 = none seen
  bool charging = false;  // the latest update counted as charging
};

// Feeds one reading (volts <= 0 = unknown: ignored). Returns true when the
// charge itself changed (started, still going, or a new peak), i.e. when
// the state should be persisted right away.
bool updateCharge(ChargeState& s, float volts, uint32_t now);

// Compact duration: "45m", "1h 20m", "2h", "14h", "3d 4h", "3d", "12d". buf >= 12.
void formatDuration(uint32_t seconds, char* buf, int bufLen);

// Coarsens an age so its formatDuration() text changes rarely: 5-min steps
// (at least 5m) below 1 h, 30-min steps below 10 h, whole hours above. The
// screen is only redrawn when its content changes, so a per-minute "for 23m"
// would cost a refresh on every wake.
uint32_t quantizeAge(uint32_t seconds);

// Device battery % in 5 % steps with hysteresis: the shown step changes only
// once the reading is >= 4 % away from it, so ADC jitter around a boundary
// does not flip the status bar. shown < 0 = nothing shown yet; raw < 0 = unknown.
int stickyBatteryStep(int rawPercent, int shownStep);

// Integer display value with hysteresis: keeps `shown` while |raw - shown| <
// threshold (> 0.5), so a sensor hovering around x.5 does not flip the digit.
int stickyRound(float raw, int shown, bool hasShown, float threshold);

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
// The countdown ("in 1h 20m") is rounded up to 5 min within the hour and to
// 10 min beyond, so the text does not change on every wake.
bool formatOutageStatus(const OutageSchedule& s, int nowMin, char* buf, int bufLen);

// --- SOC history -------------------------------------------------------------

constexpr uint32_t kSocBucketS = 15 * 60;

// Merges fresh raw points (any order, any density) into hist: one point per
// 15-min bucket (the newest value in it, stamped with its own time), newer
// data replacing older for the same bucket, points older than now - 24 h
// dropped, at most kHistoryMax newest kept. Returns the new count.
int mergeSocHistory(SocHistory& hist, const BatteryPoint* fresh, int freshCount, uint32_t now);

// --- connectivity --------------------------------------------------------------

enum Connectivity : uint8_t {
  CONN_UNKNOWN = 0,
  CONN_OK = 1,
  CONN_NO_INTERNET = 2,  // WiFi up, but no request got any HTTP response
  CONN_NO_WIFI = 3,
};

// State after an online attempt. attempts = HTTPS requests tried this wake,
// responses = requests that got any HTTP status (even an error), transportErrors
// = requests that failed below HTTP (DNS, TCP, TLS). Returns `previous` when
// nothing was tried, so an idle wake keeps the last known state.
Connectivity classifyConnectivity(bool wifiUp, int attempts, int responses, int transportErrors,
                                  Connectivity previous);

}  // namespace dash
