// Cross-wake state and last-known-good data caches, persisted in NVS so they
// survive deep sleep, brownout and battery swaps (RTC RAM would not).
//
// Cache blobs are the packed POD structs from dashcore, framed with
// {kCacheVersion, crc32}. A version or CRC mismatch reads as "no cache".
#pragma once

#include <Arduino.h>
#include <data_model.h>

// SRC_BACKUP = Deye status tiles, SRC_SOC = Deye 24 h SOC graph (own cadence),
// SRC_ALERT = air raid alerts.
enum Source : uint8_t {
  SRC_WEATHER = 0,
  SRC_OUTAGE = 1,
  SRC_BACKUP = 2,
  SRC_FX = 3,
  SRC_SOC = 4,
  SRC_ALERT = 5,
  SRC_COUNT = 6
};

struct PersistedState {
  uint32_t lastSuccessEpoch[SRC_COUNT];
  uint32_t lastSntpEpoch;
  uint32_t bootCount;
  uint16_t consecWifiFails;
  uint32_t lastOnlineEpoch;  // last wake with WiFi (status bar "No WiFi for ...")
  uint32_t lastInternetEpoch;  // last wake where a request got an HTTP response
  uint32_t lastOtaCheckEpoch;  // last periodic update check
  // Device battery charge detection (dash::updateCharge): peak and lowest
  // reading of the last charge, and the last wake that counted as charging.
  float chargePeakV;
  float chargeMinV;
  uint32_t chargeEndEpoch;  // 0 = no reading yet
  bool chargeEndEstimated;  // no charge seen: chargeEndEpoch is the first reading
  uint8_t lastPage;          // 0 = power, 1 = fx
  String otaTriedVersion;  // set just before rebooting into a new image
  String otaBadVersion;    // a version that was rolled back; never retried
  String deyeToken;
  uint32_t deyeTokenExpEpoch;
};

namespace state_store {

void load(PersistedState& st);
// Written on online wakes (and page switches) only: offline wakes can come
// every few minutes, and their state lives in RTC RAM (rtc_state.h).
void save(const PersistedState& st);

// Cache blob IO. Returns false when absent, version-mismatched or corrupt.
bool loadBlob(Source src, void* out, size_t size);
// Skips the NVS write when the stored bytes are already identical (wear).
void saveBlob(Source src, const void* data, size_t size);

}  // namespace state_store
