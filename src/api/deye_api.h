#pragma once

#include <data_model.h>

#include "../store/config_store.h"
#include "../store/state_store.h"

namespace deye_api {

// Full backup-power snapshot: auth (token cached in PersistedState across
// deep sleeps), /device/latest, /device/historyRaw downsampled to <= 96 pts.
// Mutates st.deyeToken / st.deyeTokenExpEpoch on (re)auth.
bool fetch(const Config& cfg, PersistedState& st, dash::BackupData& out);

}  // namespace deye_api
