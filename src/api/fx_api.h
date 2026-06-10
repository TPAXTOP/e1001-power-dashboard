#pragma once

#include <data_model.h>

#include "../store/config_store.h"

namespace fx_api {

// exchangerate.host /timeframe history (29 days), shown on the FX page.
bool fetch(const Config& cfg, dash::FxData& out);

}  // namespace fx_api
