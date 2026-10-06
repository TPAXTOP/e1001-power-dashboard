#pragma once

#include <data_model.h>

#include "../store/config_store.h"

namespace yasno_api {

// Yasno planned-outages for the configured group. The response contains all
// groups; a deserialization filter keeps only ours so peak RAM stays small.
// On failure, *error (if given) says why, for the on-screen "no data" note.
enum FetchError : uint8_t { ERR_NONE = 0, ERR_REQUEST = 1, ERR_GROUP_MISSING = 2 };
bool fetch(const Config& cfg, dash::OutageSchedule& out, FetchError* error = nullptr);

}  // namespace yasno_api
