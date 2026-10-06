#pragma once

#include <stdint.h>

#include "../store/config_store.h"
#include "../store/state_store.h"

namespace wake_cycle {

// One full wake: time sync, conditional fetches, render, OTA check.
// pageButton = woken by a white button (cycles power <-> fx page).
// coldBoot = power-on/reset/flash (not a deep-sleep wake): forces SNTP.
// Returns the number of seconds to deep-sleep afterwards.
uint32_t run(Config& cfg, PersistedState& st, bool pageButton, bool coldBoot);

}  // namespace wake_cycle
