// Plain data structs shared by API parsers, NVS cache blobs and renderers.
// Pure C++ (no Arduino) so lib/dashcore also builds for native host tests.
//
// All structs are fixed-size PODs: they are persisted verbatim as NVS blobs,
// framed with {version, crc32} by the state store. Bump kCacheVersion when
// any layout changes so old blobs are discarded instead of misread.
#pragma once

#include <stdint.h>

namespace dash {

constexpr uint8_t kCacheVersion = 1;

constexpr int kHourlyMax = 8;     // Open-Meteo forecast_hours
constexpr int kHistoryMax = 96;   // battery graph points (15-min x 24h)
constexpr int kSlotsMax = 24;     // outage slots per day
constexpr int kFxMax = 31;        // 29-day FX history + margin

// ---------------------------------------------------------------- weather

struct HourlyForecast {
  char time[20];        // local Kyiv "YYYY-MM-DDTHH:MM" as returned by Open-Meteo
  float temperature;
  int16_t weatherCode;
};

struct WeatherData {
  float temperature;
  int16_t humidity;
  float windSpeed;
  int16_t weatherCode;
  char time[20];        // local Kyiv time of the current reading
  uint8_t hourlyCount;
  HourlyForecast hourly[kHourlyMax];
};

// ---------------------------------------------------------------- outage

enum SlotType : uint8_t { SLOT_NOT_PLANNED = 0, SLOT_DEFINITE = 1 };

struct OutageSlot {
  uint16_t startMin;    // minutes from midnight
  uint16_t endMin;
  uint8_t type;         // SlotType
};

struct OutageDay {
  bool present;
  char date[11];        // "YYYY-MM-DD"
  char status[28];      // "ScheduleApplies" | "EmergencyShutdowns" | ...
  uint8_t slotCount;
  OutageSlot slots[kSlotsMax];
};

struct OutageSchedule {
  OutageDay today;
  OutageDay tomorrow;
  char groupId[8];
  char updatedOn[28];   // ISO timestamp from Yasno
};

enum HalfAffected : uint8_t { HALF_NONE = 0, HALF_FIRST = 1, HALF_SECOND = 2, HALF_BOTH = 3 };

struct HourlyOutage {
  uint8_t hour;         // 0-23
  float fraction;       // 0..1
  uint8_t halfAffected; // HalfAffected
};

struct DayOutages {
  HourlyOutage hours[24];
  bool scheduleApplies; // status == "ScheduleApplies"
};

// ---------------------------------------------------------------- backup power

enum ChargingStatus : uint8_t {
  CHARGE_UNKNOWN = 0,
  CHARGE_DISCHARGING = 1,
  CHARGE_CHARGING = 2,
  CHARGE_IDLE = 3,
};

struct BatteryPoint {
  uint32_t epoch;       // unix seconds
  float percent;        // 0-100
};

struct BackupData {
  float batteryPercent;
  bool gridConnected;
  uint32_t lastUpdateEpoch;   // inverter collectionTime, unix seconds
  bool hasBatteryPower;
  float batteryPowerWatts;    // + discharging / - charging
  bool hasLoadPower;
  float loadPowerWatts;
  uint8_t chargingStatus;     // ChargingStatus
  bool hasRuntime;
  int32_t estimatedRuntimeMin;
  uint8_t historyCount;
  BatteryPoint history[kHistoryMax];
};

// ---------------------------------------------------------------- fx

struct FxPoint {
  char date[11];        // "YYYY-MM-DD"
  float value;
};

struct FxData {
  uint8_t count;
  FxPoint points[kFxMax];
  FxPoint latest;
  float minValue;
  float maxValue;
  uint32_t updatedAtEpoch;
};

}  // namespace dash
