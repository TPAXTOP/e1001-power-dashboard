#include "render_statusbar.h"

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>
#include <status.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "widgets.h"

namespace render_statusbar {

static Adafruit_GFX& g() { return display::gfx(); }
static U8G2_FOR_ADAFRUIT_GFX& f() { return display::u8g2(); }

static const int kMidY = kTop + 16;  // vertical center of the 28px content area

// Right block, right-aligned at x=792: [wifi] [batt] 80% · 3d 4h · 6%/d
// Returns the x where the block starts.
static int renderRight(const StatusBarView& v) {
  char text[40];
  int n;
  if (v.batteryPercent >= 0) {
    n = snprintf(text, sizeof(text), "%d%%", v.batteryPercent);  // already a 5 % step
  } else {
    n = snprintf(text, sizeof(text), "--%%");
  }
  if (v.hasSinceFull) {
    char since[12];
    dash::formatDuration(v.sinceFullS, since, sizeof(since));
    n += snprintf(text + n, sizeof(text) - n, " \xC2\xB7 %s", since);
    if (v.drainPerDay >= 0) {
      snprintf(text + n, sizeof(text) - n, " \xC2\xB7 %d%%/d", v.drainPerDay);
    }
  }

  display::setFont(u8g2_font_helvB08_tf);
  const int baseline = kMidY + 4;
  int x = 792 - f().getUTF8Width(text);
  f().setCursor(x, baseline);
  f().print(text);

  x -= 6 + 24;
  widgets::drawMiniBattery(x, kMidY - 5, v.batteryPercent);

  if (v.connectivity != dash::CONN_UNKNOWN) {
    x -= 12 + widgets::kWifiIconW;
    widgets::drawWifiIcon(x, kMidY, v.connectivity);
  }
  return x;
}

static void renderMessage(const StatusBarView& v, int right) {
  if (v.severity == StatusBarView::SEV_NONE || !v.message[0]) return;

  int x = 10;
  if (v.severity == StatusBarView::SEV_ALERT) {
    g().fillRect(0, kTop + 2, right, display::kHeight - kTop - 2, GxEPD_BLACK);
    f().setForegroundColor(GxEPD_WHITE);
  } else if (v.severity == StatusBarView::SEV_WARN) {
    widgets::drawStaleBadge(8, kMidY);
    x = 30;
  }

  // Fall back to a smaller font rather than running into the right block.
  display::setFont(u8g2_font_helvB12_tf);
  if (x + f().getUTF8Width(v.message) > right - 8) display::setFont(u8g2_font_helvB10_tf);
  f().setCursor(x, kMidY + 6);
  f().print(v.message);
  f().setForegroundColor(GxEPD_BLACK);
}

void render(const StatusBarView& view) {
  g().fillRect(0, kTop, display::kWidth, display::kHeight - kTop, GxEPD_WHITE);
  g().fillRect(0, kTop, display::kWidth, 2, GxEPD_BLACK);

  int right = renderRight(view) - 10;
  g().drawFastVLine(right, kTop + 7, 16, GxEPD_BLACK);
  renderMessage(view, right);
}

}  // namespace render_statusbar
