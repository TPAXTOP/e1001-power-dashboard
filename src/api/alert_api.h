#pragma once

#include <data_model.h>

#include "../store/config_store.h"

namespace alert_api {

enum FetchError : uint8_t {
  ERR_NONE = 0,
  ERR_REQUEST = 1,  // network, HTTP or JSON failure
  ERR_AUTH = 2,     // 401/403: API key missing or rejected
};

// The NVS cache blob (SRC_ALERT): the shown alert plus what is needed to
// skip unchanged refetches.
struct AlertCache {
  dash::AlertStatus status;
  int64_t actionIndex;    // /api/v3/alerts/status lastActionIndex at fetch time; -1 = unknown
  uint32_t fetchedEpoch;  // last full fetch of the region(s)
  char regions[32];       // Config::alertRegions the data belongs to
};

// Air raid alerts of the configured region(s) via api.ukrainealarm.com v3,
// reduced to the one alert the status bar shows. As the API docs ask, the
// cheap /api/v3/alerts/status is checked first: when its lastActionIndex
// equals prev's (and prev is for the same regions and < 15 min old), prev is
// still current and no region is fetched. Otherwise one /api/v3/alerts/{id}
// request per region id (max 3). An all-clear is a success with
// out.status.level == ALERT_NONE.
bool fetch(const Config& cfg, const AlertCache* prev, uint32_t now, AlertCache& out,
           FetchError* error);

}  // namespace alert_api
