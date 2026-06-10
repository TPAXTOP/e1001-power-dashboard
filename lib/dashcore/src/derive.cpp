#include "derive.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace dash {

// ---------------------------------------------------------------- outage

void slotsToHourlyFractions(const OutageSlot* slots, int count, HourlyOutage out[24]) {
  int firstHalfMinutes[24] = {0};
  int secondHalfMinutes[24] = {0};

  for (int s = 0; s < count; s++) {
    if (slots[s].type != SLOT_DEFINITE) continue;
    for (int minute = slots[s].startMin; minute < slots[s].endMin && minute < 1440; minute++) {
      int hour = minute / 60;
      int minuteInHour = minute % 60;
      if (hour >= 24) break;
      if (minuteInHour < 30) {
        firstHalfMinutes[hour]++;
      } else {
        secondHalfMinutes[hour]++;
      }
    }
  }

  for (int hour = 0; hour < 24; hour++) {
    int firstHalf = firstHalfMinutes[hour] > 30 ? 30 : firstHalfMinutes[hour];
    int secondHalf = secondHalfMinutes[hour] > 30 ? 30 : secondHalfMinutes[hour];
    float fraction = (firstHalf + secondHalf) / 60.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    uint8_t half;
    if (firstHalf > 0 && secondHalf > 0) {
      half = HALF_BOTH;
    } else if (firstHalf > 0) {
      half = HALF_FIRST;
    } else if (secondHalf > 0) {
      half = HALF_SECOND;
    } else {
      half = HALF_NONE;
    }

    out[hour] = {static_cast<uint8_t>(hour), fraction, half};
  }
}

void generateAlternatingPattern(bool startWithFirst, HourlyOutage out[24]) {
  for (int hour = 0; hour < 24; hour++) {
    bool isEvenHour = (hour % 2) == 0;
    uint8_t half = startWithFirst ? (isEvenHour ? HALF_FIRST : HALF_SECOND)
                                  : (isEvenHour ? HALF_SECOND : HALF_FIRST);
    out[hour] = {static_cast<uint8_t>(hour), 0.5f, half};
  }
}

static void emptyDay(HourlyOutage out[24]) {
  for (int hour = 0; hour < 24; hour++) {
    out[hour] = {static_cast<uint8_t>(hour), 0.0f, HALF_NONE};
  }
}

static bool isEmergencyNoSlots(const OutageDay& day) {
  return day.present && strcmp(day.status, "EmergencyShutdowns") == 0 && day.slotCount == 0;
}

void getHourlyOutages(const OutageSchedule* schedule, DayOutages& today, DayOutages& tomorrow) {
  if (!schedule) {
    emptyDay(today.hours);
    emptyDay(tomorrow.hours);
    today.scheduleApplies = false;
    tomorrow.scheduleApplies = false;
    return;
  }

  if (isEmergencyNoSlots(schedule->today)) {
    generateAlternatingPattern(true, today.hours);  // today starts with the left half
  } else if (schedule->today.present) {
    slotsToHourlyFractions(schedule->today.slots, schedule->today.slotCount, today.hours);
  } else {
    emptyDay(today.hours);
  }

  if (isEmergencyNoSlots(schedule->tomorrow)) {
    generateAlternatingPattern(false, tomorrow.hours);  // tomorrow starts with the right half
  } else if (schedule->tomorrow.present) {
    slotsToHourlyFractions(schedule->tomorrow.slots, schedule->tomorrow.slotCount, tomorrow.hours);
  } else {
    emptyDay(tomorrow.hours);
  }

  today.scheduleApplies =
      schedule->today.present && strcmp(schedule->today.status, "ScheduleApplies") == 0;
  tomorrow.scheduleApplies =
      schedule->tomorrow.present && strcmp(schedule->tomorrow.status, "ScheduleApplies") == 0;
}

// ---------------------------------------------------------------- backup power

ChargingStatus chargingStatusFrom(bool hasPower, float batteryPowerWatts, float thresholdW) {
  if (!hasPower) return CHARGE_UNKNOWN;
  if (batteryPowerWatts > thresholdW) return CHARGE_DISCHARGING;
  if (batteryPowerWatts < -thresholdW) return CHARGE_CHARGING;
  return CHARGE_IDLE;
}

int32_t runtimeMinutes(float capacityWh, float batteryPercent, float dischargePowerWatts) {
  if (dischargePowerWatts <= 0) return -1;
  float availableEnergyWh = capacityWh * (batteryPercent / 100.0f);
  float runtimeHours = availableEnergyWh / dischargePowerWatts;
  return static_cast<int32_t>(lroundf(runtimeHours * 60.0f));
}

int downsampleHistory(BatteryPoint* points, int count, int target) {
  if (count <= target || target < 2) return count;
  float step = static_cast<float>(count - 1) / static_cast<float>(target - 1);
  // In-place is safe: source index i*step always >= destination index i.
  for (int i = 0; i < target; i++) {
    points[i] = points[static_cast<int>(lroundf(i * step))];
  }
  return target;
}

// ---------------------------------------------------------------- formatting

void formatRuntime(int32_t minutes, char* buf, int bufLen) {
  if (minutes < 0) {
    snprintf(buf, bufLen, "--");
  } else if (minutes < 60) {
    snprintf(buf, bufLen, "~%dm", static_cast<int>(minutes));
  } else {
    int hours = minutes / 60;
    int remaining = minutes % 60;
    if (hours >= 10 || remaining == 0) {
      snprintf(buf, bufLen, "~%dh", hours);
    } else {
      snprintf(buf, bufLen, "~%dh%dm", hours, remaining);
    }
  }
}

void formatPower(bool has, float watts, char* buf, int bufLen) {
  if (!has) {
    snprintf(buf, bufLen, "--");
    return;
  }
  float absWatts = fabsf(watts);
  if (absWatts >= 1000.0f) {
    snprintf(buf, bufLen, "%.1fkW", absWatts / 1000.0);
  } else {
    snprintf(buf, bufLen, "%dW", static_cast<int>(lroundf(absWatts)));
  }
}

// ---------------------------------------------------------------- weather

WeatherIcon weatherCodeToIcon(int code) {
  if (code == 0 || code == 1) return ICON_SUNNY;
  if (code == 2) return ICON_PARTLY_CLOUDY;
  if (code == 3 || code == 45 || code == 48) return ICON_CLOUDY;
  if (code >= 51 && code <= 67) return ICON_RAIN;
  if (code >= 80 && code <= 82) return ICON_RAIN;
  if (code >= 71 && code <= 77) return ICON_CLOUDY;
  if (code >= 85 && code <= 86) return ICON_CLOUDY;
  if (code >= 95) return ICON_RAIN;
  return ICON_CLOUDY;
}

const char* describeWeather(int code) {
  switch (code) {
    case 0: return "Clear sky";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: return "Fog";
    case 48: return "Depositing rime fog";
    case 51: return "Light drizzle";
    case 53: return "Moderate drizzle";
    case 55: return "Dense drizzle";
    case 56: return "Light freezing drizzle";
    case 57: return "Dense freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Moderate rain";
    case 65: return "Heavy rain";
    case 66: return "Light freezing rain";
    case 67: return "Heavy freezing rain";
    case 71: return "Light snow";
    case 73: return "Moderate snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light rain showers";
    case 81: return "Moderate rain showers";
    case 82: return "Violent rain showers";
    case 85: return "Light snow showers";
    case 86: return "Heavy snow showers";
    case 95: return "Thunderstorm";
    case 96: return "Thunderstorm with hail";
    case 99: return "Thunderstorm with heavy hail";
    default: return "N/A";
  }
}

void formatHourlyTime(const char* isoLocal, char* buf, int bufLen) {
  const char* t = isoLocal ? strchr(isoLocal, 'T') : nullptr;
  if (t && strlen(t + 1) >= 5) {
    snprintf(buf, bufLen, "%.5s", t + 1);
  } else {
    snprintf(buf, bufLen, "--:--");
  }
}

}  // namespace dash
