#pragma once

#include <stdint.h>

// Screen-wide bar at y 450-480, drawn on every page. Left: the single most
// important message. Right: render time + device battery.
struct StatusBarView {
  enum Severity : uint8_t {
    SEV_NONE = 0,
    SEV_INFO = 1,   // plain text (outage countdown)
    SEV_WARN = 2,   // "!" badge (WiFi down, stale data, low battery)
    SEV_ALERT = 3,  // whole message area inverted (future: air raid alerts)
  };
  uint8_t severity = SEV_NONE;
  char message[72] = "";

  uint8_t connectivity = 0;  // dash::Connectivity, icon left of the battery
  int batteryPercent = -1;   // 5 % step, -1 = unknown reading
  bool hasSinceFull = false;
  uint32_t sinceFullS = 0;   // time since the last full charge (quantized)
  int drainPerDay = -1;      // % per 24 h since full, -1 = not enough data yet
};

namespace render_statusbar {

constexpr int kTop = 450;  // pages must keep y >= kTop free

void render(const StatusBarView& view);

}  // namespace render_statusbar
