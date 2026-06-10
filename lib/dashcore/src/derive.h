// Pure display-derivation logic, ported 1:1 from the web dashboard
// (lib/data-fetchers.ts, lib/deye-api.ts, lib/power-format-utils.ts,
// lib/weather-helpers.ts, lib/weather-codes.ts). No Arduino dependencies;
// unit-tested on the host via test/test_derive.
#pragma once

#include "data_model.h"

namespace dash {

// --- outage schedule -> 24 hour tiles -------------------------------------

// Convert minute slots to per-hour fractions with half-hour attribution.
void slotsToHourlyFractions(const OutageSlot* slots, int count, HourlyOutage out[24]);

// Zebra pattern shown for EmergencyShutdowns days that have no slots.
// startWithFirst=true: even hours get the left (first-half) triangle.
void generateAlternatingPattern(bool startWithFirst, HourlyOutage out[24]);

// Full conversion incl. EmergencyShutdowns handling and scheduleApplies flags.
// schedule == nullptr means "no data at all".
void getHourlyOutages(const OutageSchedule* schedule, DayOutages& today, DayOutages& tomorrow);

// --- backup power ----------------------------------------------------------

// Positive = discharging, negative = charging, |w| <= thresholdW = idle.
ChargingStatus chargingStatusFrom(bool hasPower, float batteryPowerWatts, float thresholdW = 50.0f);

// (capacityWh * soc/100) / dischargeW * 60, rounded. Returns -1 when not discharging.
int32_t runtimeMinutes(float capacityWh, float batteryPercent, float dischargePowerWatts);

// Even downsampling that always keeps the newest point. Returns new count.
int downsampleHistory(BatteryPoint* points, int count, int target);

// --- formatting ------------------------------------------------------------

// "~45m", "~2h30m", "~18h", "--" (when minutes < 0). buf >= 12 bytes.
void formatRuntime(int32_t minutes, char* buf, int bufLen);

// "850W", "1.3kW", "--" (when !has). Absolute value. buf >= 12 bytes.
void formatPower(bool has, float watts, char* buf, int bufLen);

// --- weather ---------------------------------------------------------------

enum WeatherIcon : uint8_t { ICON_SUNNY, ICON_PARTLY_CLOUDY, ICON_CLOUDY, ICON_RAIN };

WeatherIcon weatherCodeToIcon(int code);
const char* describeWeather(int code);  // "Partly cloudy" / "N/A"

// "YYYY-MM-DDTHH:MM" -> "HH:MM" into buf (>= 6 bytes); "--:--" if malformed.
void formatHourlyTime(const char* isoLocal, char* buf, int bufLen);

}  // namespace dash
