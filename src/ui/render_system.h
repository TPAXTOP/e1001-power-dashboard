#pragma once

namespace render_system {

// First-boot provisioning instructions (AP credentials + portal address).
void renderSetup(const char* apSsid, const char* apPass, const char* ip);

// Maintenance mode banner with the portal URL.
void renderMaintenance(const char* ip);

// Critical battery screen shown once before button-only deep sleep.
void renderBatteryEmpty(float vbat);

}  // namespace render_system
