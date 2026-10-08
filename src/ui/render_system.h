#pragma once

namespace render_system {

// First-boot provisioning instructions (AP credentials + portal address).
void renderSetup(const char* apSsid, const char* apPass, const char* ip);

// Maintenance mode banner with the portal URL. apSsid/apPass are non-null
// when home WiFi failed and the portal fell back to its own access point.
void renderMaintenance(const char* ip, const char* apSsid = nullptr,
                       const char* apPass = nullptr);

// Critical battery screen shown once before button-only deep sleep. `ran`
// is how long it ran since the last charge ("" = unknown).
void renderBatteryEmpty(float vbat, const char* ran, const char* version);

}  // namespace render_system
