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

// One wake: run whatever is due (sources, indoor sensor, OTA check), render,
// refresh the panel only if the frame changed. WiFi is switched on only when
// an online task is due. Returns the seconds to deep-sleep until the next
// task, outage start/end, night edge or full hour.
uint32_t run(Config& cfg, PersistedState& st, const Wake& wake);

}  // namespace wake_cycle
