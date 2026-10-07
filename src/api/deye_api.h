#pragma once

#include <data_model.h>

#include "../store/config_store.h"
#include "../store/state_store.h"

namespace deye_api {

// Both calls authenticate as needed (token cached in PersistedState across
// deep sleeps) and mutate st.deyeToken / st.deyeTokenExpEpoch on (re)auth.

// Status tiles: /device/latest (battery %, grid, battery and load power).
bool fetchStatus(const Config& cfg, PersistedState& st, dash::BackupData& out);

// 24 h SOC graph: /device/historyRaw for the time after the newest cached
// point (the full 24 h when hist is empty), merged into 15-min buckets.
// hist is updated in place; on failure it is left unchanged.
bool fetchSoc(const Config& cfg, PersistedState& st, dash::SocHistory& hist);

}  // namespace deye_api
