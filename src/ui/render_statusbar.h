#pragma once

#include <stdint.h>

// Screen-wide bar at y 450-480, drawn on every page. Left: the single most
// important message. Right: connectivity + device battery.
struct StatusBarView {
  enum Severity : uint8_t {
    SEV_NONE = 0,
    SEV_INFO = 1,          // plain text (outage countdown)
    SEV_WARN = 2,          // "!" badge (WiFi down, stale data, low battery)
    SEV_ALERT_YELLOW = 3,  // dithered bar, message on a white plate
    SEV_ALERT_RED = 4,     // whole bar inverted (black, white text)
  };
  uint8_t severity = SEV_NONE;
  // UTF-8. Alerts are Ukrainian (the API's own text, up to ~50 letters).
  char message[128] = "";

  uint8_t connectivity = 0;  // dash::Connectivity, icon left of the battery
  int batteryPercent = -1;   // 5 % step, -1 = unknown reading
  bool charging = false;     // the battery reading says "on the charger"
  bool hasSinceCharge = false;
  uint32_t sinceChargeS = 0;  // wall-clock time since the last charge ended (quantized)
};

namespace render_statusbar {

constexpr int kTop = 450;  // pages must keep y >= kTop free

void render(const StatusBarView& view);

}  // namespace render_statusbar
