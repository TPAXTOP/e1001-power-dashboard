// Dashboard frames: render, decide whether and how to refresh, and remember
// what is on the panel across deep sleep.
//
// The whole frame is described by a Frame (view structs only; rendering is a
// pure function of it). After each push the Frame is kept in RTC RAM, so the
// next wake can (a) skip the panel entirely when the new frame is pixel-
// identical, and (b) redraw the old frame to feed the controller's "old" RAM
// for a partial refresh.
#pragma once

#include <data_model.h>
#include <stdint.h>

#include "render_power.h"
#include "render_statusbar.h"

namespace screen {

enum Page : uint8_t { PAGE_POWER = 0, PAGE_FX = 1 };

struct Frame {
  uint8_t page = PAGE_POWER;
  PowerView power;
  StatusBarView bar;
  bool hasFx = false;
  bool fxStale = false;
  dash::FxData fx;
};

struct Policy {
  uint32_t now = 0;             // 0 = clock unknown: time-based rules are skipped
  bool forceFull = false;       // cold boot, manual refresh
  bool night = false;           // no time-based full refresh at night
  uint16_t fullRefreshMin = 0;  // 0 = off
  uint16_t maxPartials = 0;     // 0 = no limit
  bool cold = false;            // panel too cold for the fast partial waveform
};

enum Result : uint8_t { UNCHANGED = 0, PARTIAL = 1, FULL = 2 };

// Renders `f` and refreshes the panel only when something changed. Panel ends
// in deep sleep. Returns what was done.
Result present(const Frame& f, const Policy& p);

// Forget the panel state (a system screen was drawn, or the panel is unknown).
void invalidate();

}  // namespace screen
