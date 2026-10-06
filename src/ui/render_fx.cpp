#include "render_fx.h"

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>
#include <math.h>
#include <stdio.h>

#include <algorithm>

#include "display.h"
#include "widgets.h"

namespace render_fx {

static Adafruit_GFX& g() { return display::gfx(); }
static U8G2_FOR_ADAFRUIT_GFX& f() { return display::u8g2(); }

void render(bool hasFx, bool stale, const dash::FxData& fx) {
  display::clear();

  display::setFont(u8g2_font_helvB12_tf);
  f().setCursor(40, 44);
  f().print("USD / UAH - 30 days");
  if (stale) widgets::drawStaleBadge(f().getCursorX() + 8, 38);
  g().fillRect(40, 56, 720, 2, GxEPD_BLACK);

  if (!hasFx || fx.count < 2) {
    display::setFont(u8g2_font_helvB10_tf);
    f().setCursor(40, 120);
    f().print("No data");
    return;
  }

  // headline metrics: today / 30d min / 30d max
  const char* labels[3] = {"Today", "30d min", "30d max"};
  float values[3] = {fx.latest.value, fx.minValue, fx.maxValue};
  for (int i = 0; i < 3; i++) {
    int x = 40 + i * 250;
    display::setFont(u8g2_font_helvB10_tf);
    f().setCursor(x, 92);
    f().print(labels[i]);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", values[i]);
    display::setFont(u8g2_font_logisoso22_tf);
    f().setCursor(x, 130);
    f().print(buf);
  }

  // chart area: 720x270 at (40,160), padded like the web FxChart
  const int cx = 40, cy = 160, cw = 720, ch = 270;
  const int padL = 56, padR = 10, padT = 10, padB = 30;
  const int gw = cw - padL - padR, gh = ch - padT - padB;
  float lo = fx.minValue, hi = fx.maxValue;
  if (hi - lo < 0.01f) {  // flat series: pad the range so the line is visible
    hi += 0.05f;
    lo -= 0.05f;
  }

  auto sx = [&](int i) { return cx + padL + (int)lroundf((float)i / (fx.count - 1) * gw); };
  auto sy = [&](float v) {
    return cy + padT + gh - (int)lroundf((v - lo) / (hi - lo) * gh);
  };

  // Y labels + dashed grid at min/mid/max
  display::setFont(u8g2_font_helvB08_tf);
  float yVals[3] = {hi, (hi + lo) / 2, lo};
  for (int i = 0; i < 3; i++) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", yVals[i]);
    int tw = f().getUTF8Width(buf);
    int yy = sy(yVals[i]);
    f().setCursor(cx + padL - 8 - tw, yy + 4);
    f().print(buf);
    for (int gx = cx + padL; gx < cx + cw - padR; gx += 10) {
      g().drawFastHLine(gx, yy, std::min(4, cx + cw - padR - gx), GxEPD_BLACK);
    }
  }
  g().fillRect(cx + padL, cy + padT, 2, gh, GxEPD_BLACK);
  g().fillRect(cx + padL, cy + padT + gh, gw, 2, GxEPD_BLACK);

  // X labels: ~6 date ticks "MM-DD"
  int step = std::max(1, (int)fx.count / 6);
  display::setFont(u8g2_font_6x10_tf);
  for (int i = 0; i < fx.count; i += step) {
    const char* d = fx.points[i].date;  // "YYYY-MM-DD"
    if (strlen(d) >= 10) {
      char lbl[6];
      snprintf(lbl, sizeof(lbl), "%.5s", d + 5);
      int tw = f().getUTF8Width(lbl);
      f().setCursor(sx(i) - tw / 2, cy + padT + gh + 16);
      f().print(lbl);
    }
  }

  // polyline + point markers
  for (int i = 0; i + 1 < fx.count; i++) {
    widgets::thickLine(sx(i), sy(fx.points[i].value), sx(i + 1), sy(fx.points[i + 1].value), 3,
                       GxEPD_BLACK);
  }
  for (int i = 0; i < fx.count; i++) {
    g().fillCircle(sx(i), sy(fx.points[i].value), 3, GxEPD_BLACK);
  }
}

}  // namespace render_fx
