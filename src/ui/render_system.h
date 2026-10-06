#pragma once

namespace render_system {

// First-boot provisioning instructions (AP credentials + portal address).
void renderSetup(const char* apSsid, const char* apPass, const char* ip);

// Maintenance mode banner with the portal URL. apSsid/apPass are non-null
// when home WiFi failed and the portal fell back to its own access point.
void renderMaintenance(const char* ip, const char* apSsid = nullptr,
                       const char* apPass = nullptr);

// Critical battery screen shown once before button-only deep sleep.
void renderBatteryEmpty(float vbat);

}  // namespace render_system
