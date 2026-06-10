#pragma once

#include "../store/config_store.h"

namespace ota_pull {

// Fetch the manifest (cfg.otaManifestUrl -> {"version","url"}), compare
// against APP_VERSION, stream the new image into the inactive OTA slot.
// Returns true when an update was installed (caller reboots).
bool checkAndUpdate(const Config& cfg);

// Call after a wake cycle completed successfully on a freshly-updated image.
void markImageValid();

}  // namespace ota_pull
