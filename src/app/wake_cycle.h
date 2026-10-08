#pragma once

#include <stdint.h>

#include "../store/config_store.h"
#include "../store/state_store.h"

namespace wake_cycle {

struct Wake {
  bool pageButton = false;     // white button: cycle power <-> fx page
  bool refreshButton = false;  // green short press: fetch everything now, full refresh
  bool coldBoot = false;       // power-on/reset/flash (not a deep-sleep wake)
};

// One wake: run whatever is due on this slot of the wake grid (sources, OTA
// check; the indoor sensor is read every wake), render, refresh the panel
// only if the frame changed. WiFi is switched on only when an online task is
// due. Returns the seconds to deep-sleep until the next grid slot (the grid
// period is the shortest enabled interval, see schedule.h).
uint32_t run(Config& cfg, PersistedState& st, const Wake& wake);

}  // namespace wake_cycle
