// Cross-wake state and last-known-good data caches, persisted in NVS so they
// survive deep sleep, brownout and battery swaps (RTC RAM would not).
//
// Cache blobs are the packed POD structs from dashcore, framed with
// {kCacheVersion, crc32}. A version or CRC mismatch reads as "no cache".
#pragma once

#include <Arduino.h>
#include <data_model.h>

enum Source : uint8_t { SRC_WEATHER = 0, SRC_OUTAGE = 1, SRC_BACKUP = 2, SRC_FX = 3, SRC_COUNT = 4 };

struct PersistedState {
  uint32_t lastSuccessEpoch[SRC_COUNT];
  uint32_t lastSntpEpoch;
  uint32_t bootCount;
  uint16_t consecWifiFails;
  uint32_t lastOnlineEpoch;  // last wake with WiFi (status bar "No WiFi for ...")
  uint32_t lastFullEpoch;    // last wake with vbat >= vbatFull; 0 = never seen
  uint8_t lastPage;          // 0 = power, 1 = fx
  bool otaPendingVerify;
  String deyeToken;
  uint32_t deyeTokenExpEpoch;
};

namespace state_store {

void load(PersistedState& st);
void save(const PersistedState& st);

// Cache blob IO. Returns false when absent, version-mismatched or corrupt.
bool loadBlob(Source src, void* out, size_t size);
// Skips the NVS write when the stored bytes are already identical (wear).
void saveBlob(Source src, const void* data, size_t size);

}  // namespace state_store
