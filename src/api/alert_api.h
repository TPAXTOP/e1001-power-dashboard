#pragma once

#include <data_model.h>

#include "../store/config_store.h"

namespace alert_api {

enum FetchError : uint8_t {
  ERR_NONE = 0,
  ERR_REQUEST = 1,  // network, HTTP or JSON failure
  ERR_AUTH = 2,     // 401/403: API key rejected - or the key was used too often
};

// The NVS cache blob (SRC_ALERT): the shown alert and the regions it is for.
struct AlertCache {
  dash::AlertStatus status;
  uint32_t fetchedEpoch;
  char regions[32];  // Config::alertRegions the data belongs to
};

// The API refuses (401) any request with the same key less than about a
// minute after the previous one, so a check is always exactly one request:
// /api/v3/alerts/{id} for a single region id, otherwise /api/v3/alerts (all
// regions with alerts) reduced to the configured ids (max 3). The status
// endpoint is not used: it would be a second request in the same minute.
// An all-clear is a success with out.status.level == ALERT_NONE.
bool fetch(const Config& cfg, uint32_t now, AlertCache& out, FetchError* error);

}  // namespace alert_api
