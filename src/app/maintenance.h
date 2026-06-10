#pragma once

#include "../store/config_store.h"

namespace maintenance {

// Blocking portal loop. provisioning=true starts a SoftAP (first boot, no
// WiFi creds); otherwise joins the configured WiFi (falls back to AP).
// Serves the settings form + manual OTA upload. Reboots on save/timeout.
[[noreturn]] void run(Config& cfg, bool provisioning);

}  // namespace maintenance
