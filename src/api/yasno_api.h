#pragma once

#include <data_model.h>

#include "../store/config_store.h"

namespace yasno_api {

// Yasno planned-outages for the configured group. The response contains all
// groups; a deserialization filter keeps only ours so peak RAM stays small.
bool fetch(const Config& cfg, dash::OutageSchedule& out);

}  // namespace yasno_api
