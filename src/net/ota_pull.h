#pragma once

#include <Arduino.h>

#include "../store/config_store.h"
#include "../store/state_store.h"

namespace ota_pull {

struct Result {
  bool installed = false;  // new image written and activated; caller reboots
  String version;          // manifest version (when one was read)
  String message;          // human-readable outcome, for the log and the portal
};

// Fetch the manifest (cfg.otaManifestUrl -> version.json), and when it names a
// newer version than APP_VERSION (and not skipVersion), stream the image into
// the inactive OTA slot. The image is activated only when its SHA-256 matches
// the manifest and the manifest signature verifies against the embedded
// project key (certs/ota_signing_pub.pem); otherwise the slot is discarded.
Result checkAndUpdate(const Config& cfg, const String& skipVersion);

// Rollback bookkeeping, called once per boot after the state is loaded:
// a version that was installed but is not the one running was rolled back by
// the bootloader, so it becomes st.otaBadVersion and is not retried.
void noteBootedVersion(PersistedState& st);

// True while this image is fresh from an update and not yet confirmed: a
// reset in this state makes the bootloader roll back to the previous image.
bool pendingVerify();

// Confirms the running image (cancels the pending rollback). No-op otherwise.
void confirmIfPending();

}  // namespace ota_pull
