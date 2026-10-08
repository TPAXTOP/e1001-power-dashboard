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

static const int kContentTop = kTop + 2;  // below the 2 px separator line
static const int kMidY = kTop + 16;       // vertical center of the 28px content area
// White plates of the yellow alert bar: 22 px tall, 3 px of dither above/below.
static const int kPlateTop = kContentTop + 3;
static const int kPlateH = 22;

// Right block, right-aligned at x=792: [wifi] [batt] 80% · 3d 4h
// ("80% · charging" while the battery reading says it is on the charger).
// Returns the x where the block starts.
static int renderRight(const StatusBarView& v) {
  char text[40];
  int n;
  if (v.batteryPercent >= 0) {
    n = snprintf(text, sizeof(text), "%d%%", v.batteryPercent);  // already a 5 % step
  } else {
    n = snprintf(text, sizeof(text), "--%%");
  }
  if (v.charging) {
    snprintf(text + n, sizeof(text) - n, " \xC2\xB7 charging");
  } else if (v.hasSinceCharge) {
    char since[12];
    dash::formatDuration(v.sinceChargeS, since, sizeof(since));
    snprintf(text + n, sizeof(text) - n, " \xC2\xB7 %s", since);
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

static bool isAlert(const StatusBarView& v) {
  return v.severity == StatusBarView::SEV_ALERT_RED ||
         v.severity == StatusBarView::SEV_ALERT_YELLOW;
}

// Drops whole UTF-8 characters from the end of `text` and appends "..."
// until it fits in maxW with the current font.
static void ellipsize(char* text, int maxW) {
  size_t len = strlen(text);
  char trial[sizeof(StatusBarView::message) + 4];
  while (len > 0) {
    do {
      len--;
    } while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80);
    while (len > 0 && text[len - 1] == ' ') len--;
    snprintf(trial, sizeof(trial), "%.*s...", (int)len, text);
    if (f().getUTF8Width(trial) <= maxW) break;
  }
  strlcpy(text, trial, sizeof(StatusBarView::message));
}

// Alert text is Ukrainian (the API's own wording), so it needs the Cyrillic
// fonts; drawn twice 1 px apart for a bold stroke. Largest that fits wins.
static int renderAlertText(const char* message, int x, int maxW) {
  static const uint8_t* const kFonts[] = {u8g2_font_10x20_t_cyrillic, u8g2_font_9x15_t_cyrillic};
  char text[sizeof(StatusBarView::message) + 4];
  strlcpy(text, message, sizeof(text));
  for (const uint8_t* font : kFonts) {
    display::setFont(font);
    if (f().getUTF8Width(text) + 1 <= maxW) break;
  }
  if (f().getUTF8Width(text) + 1 > maxW) ellipsize(text, maxW - 1);
  for (int dx = 0; dx < 2; dx++) {
    f().setCursor(x + dx, kMidY + 6);
    f().print(text);
  }
  return x + 1 + f().getUTF8Width(text);
}

// Returns the x where the message ends (0 when there is none).
static int renderMessage(const StatusBarView& v, int right) {
  if (v.severity == StatusBarView::SEV_NONE || !v.message[0]) return 0;

  int x = 10;
  if (isAlert(v)) {
    widgets::drawAlertIcon(10, kMidY);
    x = 10 + widgets::kAlertIconW + 8;
    return renderAlertText(v.message, x, right - 8 - x);
  } else if (v.severity == StatusBarView::SEV_WARN) {
    widgets::drawStaleBadge(8, kMidY);
    x = 30;
  }

  // Fall back to a smaller font rather than running into the right block.
  display::setFont(u8g2_font_helvB12_tf);
  if (x + f().getUTF8Width(v.message) > right - 8) display::setFont(u8g2_font_helvB10_tf);
  f().setCursor(x, kMidY + 6);
  f().print(v.message);
  return x + f().getUTF8Width(v.message);
}

// Yellow alert: 50 % dither over the whole bar except two white plates (the
// message and the right block), each with a 1 px frame.
static void decorateYellow(int msgEnd, int rightStart) {
  const int plateAx = 4, plateAw = msgEnd + 8 - plateAx;
  const int plateBx = rightStart - 7, plateBw = 797 - plateBx;
  auto inPlate = [&](int x, int y) {
    if (y < kPlateTop || y >= kPlateTop + kPlateH) return false;
    return (x >= plateAx && x < plateAx + plateAw) || (x >= plateBx && x < plateBx + plateBw);
  };
  for (int y = kContentTop; y < display::kHeight; y++) {
    for (int x = 0; x < display::kWidth; x++) {
      if (((x + y) & 1) == 0 && !inPlate(x, y)) g().drawPixel(x, y, GxEPD_BLACK);
    }
  }
  g().drawRect(plateAx, kPlateTop, plateAw, kPlateH, GxEPD_BLACK);
  g().drawRect(plateBx, kPlateTop, plateBw, kPlateH, GxEPD_BLACK);
}

void render(const StatusBarView& view) {
  g().fillRect(0, kTop, display::kWidth, display::kHeight - kTop, GxEPD_WHITE);
  g().fillRect(0, kTop, display::kWidth, 2, GxEPD_BLACK);

  int rightStart = renderRight(view);
  int right = rightStart - 10;
  if (view.severity != StatusBarView::SEV_ALERT_YELLOW) {
    g().drawFastVLine(right, kTop + 7, 16, GxEPD_BLACK);
  }
  int msgEnd = renderMessage(view, right);

  if (view.severity == StatusBarView::SEV_ALERT_RED) {
    // Everything drawn black-on-white above becomes white-on-black.
    display::invertRect(0, kContentTop, display::kWidth, display::kHeight - kContentTop);
  } else if (view.severity == StatusBarView::SEV_ALERT_YELLOW) {
    decorateYellow(msgEnd, rightStart);
  }
}

}  // namespace render_statusbar
