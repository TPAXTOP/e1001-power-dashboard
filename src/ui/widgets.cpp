#include "widgets.h"

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>
#include <math.h>
#include <time.h>

#include <algorithm>

#include "display.h"

namespace widgets {

static Adafruit_GFX& g() { return display::gfx(); }

// ---------------------------------------------------------------- primitives

void thickLine(int x0, int y0, int x1, int y1, int stroke, uint16_t color) {
  // Offset perpendicular-ish to the dominant direction; good enough at our sizes.
  bool steep = abs(y1 - y0) > abs(x1 - x0);
  for (int i = 0; i < stroke; i++) {
    int off = i - stroke / 2;
    if (steep) {
      g().drawLine(x0 + off, y0, x1 + off, y1, color);
    } else {
      g().drawLine(x0, y0 + off, x1, y1 + off, color);
    }
  }
}

void fillPolygon(const float* xs, const float* ys, int n, uint16_t color, bool dither) {
  float minY = ys[0], maxY = ys[0];
  for (int i = 1; i < n; i++) {
    minY = std::min(minY, ys[i]);
    maxY = std::max(maxY, ys[i]);
  }
  for (int y = (int)ceilf(minY); y <= (int)floorf(maxY); y++) {
    float fy = y + 0.5f;
    float nodes[16];
    int count = 0;
    int j = n - 1;
    for (int i = 0; i < n; i++) {
      if ((ys[i] < fy && ys[j] >= fy) || (ys[j] < fy && ys[i] >= fy)) {
        if (count < 16) {
          nodes[count++] = xs[i] + (fy - ys[i]) / (ys[j] - ys[i]) * (xs[j] - xs[i]);
        }
      }
      j = i;
    }
    std::sort(nodes, nodes + count);
    for (int k = 0; k + 1 < count; k += 2) {
      int xStart = (int)ceilf(nodes[k]);
      int xEnd = (int)floorf(nodes[k + 1]);
      for (int x = xStart; x <= xEnd; x++) {
        if (!dither || ((x + y) & 1) == 0) {
          g().drawPixel(x, y, color);
        }
      }
    }
  }
}

void fillRectDither(int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) {
      if (((xx + yy) & 1) == 0) g().drawPixel(xx, yy, GxEPD_BLACK);
    }
  }
}

// ---------------------------------------------------------------- weather icons
// All shapes live in the 48x48 viewBox of WeatherIcons.tsx, scaled by f.

static void cloudShape(float cx, float cy, float f, float inset, uint16_t color) {
  // Cloud as a union of three lobes + base slab; inset > 0 shrinks it so a
  // white inner fill over a black outer fill leaves a ~2-3px outline.
  float baseY = cy + 10 * f - inset;
  g().fillCircle((int)(cx - 12 * f), (int)(cy + 2 * f), (int)(8 * f - inset), color);
  g().fillCircle((int)cx, (int)(cy - 4 * f), (int)(11 * f - inset), color);
  g().fillCircle((int)(cx + 13 * f), (int)(cy + 3 * f), (int)(7.5f * f - inset), color);
  g().fillRect((int)(cx - 12 * f), (int)(cy), (int)(25 * f), (int)(baseY - cy), color);
}

static void drawCloudOutline(float cx, float cy, float f, float stroke) {
  cloudShape(cx, cy, f, 0, GxEPD_BLACK);
  cloudShape(cx, cy, f, stroke, GxEPD_WHITE);
}

static void drawSun(int x, int y, float f, float cx48, float cy48, float r48, int stroke) {
  int cx = x + (int)(cx48 * f);
  int cy = y + (int)(cy48 * f);
  int r = (int)(r48 * f);
  for (int i = 0; i < stroke; i++) {
    g().drawCircle(cx, cy, r - i, GxEPD_BLACK);
  }
  // 8 rays
  for (int k = 0; k < 8; k++) {
    float a = k * (float)M_PI / 4.0f;
    int x0 = cx + (int)(cosf(a) * (r + 3 * f));
    int y0 = cy + (int)(sinf(a) * (r + 3 * f));
    int x1 = cx + (int)(cosf(a) * (r + 9 * f));
    int y1 = cy + (int)(sinf(a) * (r + 9 * f));
    thickLine(x0, y0, x1, y1, stroke, GxEPD_BLACK);
  }
}

void drawWeatherIcon(dash::WeatherIcon icon, int x, int y, int size) {
  float f = size / 48.0f;
  int stroke = size <= 24 ? 2 : 3;

  switch (icon) {
    case dash::ICON_SUNNY:
      drawSun(x, y, f, 24, 24, 10, stroke);
      break;

    case dash::ICON_CLOUDY:
      drawCloudOutline(x + 24 * f, y + 24 * f, f, stroke);
      break;

    case dash::ICON_PARTLY_CLOUDY:
      drawSun(x, y, f, 16, 14, 8, stroke);
      // cloud over the lower right, white-filled to occlude the sun
      drawCloudOutline(x + 27 * f, y + 31 * f, f * 0.8f, stroke);
      break;

    case dash::ICON_RAIN:
      drawCloudOutline(x + 24 * f, y + 17 * f, f * 0.95f, stroke);
      for (int i = 0; i < 3; i++) {
        int rx = x + (int)((14 + i * 10) * f);
        thickLine(rx, y + (int)(34 * f), rx - (int)(4 * f), y + (int)(44 * f), stroke,
                  GxEPD_BLACK);
      }
      break;
  }
}

// ---------------------------------------------------------------- power icons
// 32x48 box like the web SVGs (PowerIcons.tsx); inner shapes use a 24x24
// viewBox mapped into that box for grid/status/load.

void drawBatteryIcon(int x, int y, float pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g().drawRoundRect(x + 4, y + 8, 24, 36, 4, GxEPD_BLACK);
  g().drawRoundRect(x + 5, y + 9, 22, 34, 3, GxEPD_BLACK);
  g().fillRoundRect(x + 11, y + 2, 10, 6, 2, GxEPD_BLACK);
  int fillHeight = (int)lroundf(pct / 100.0f * 30.0f);
  if (fillHeight > 0) {
    g().fillRect(x + 7, y + 11 + (30 - fillHeight), 18, fillHeight, GxEPD_BLACK);
  }
}

// 24-viewBox -> 32x48 box (icon drawn centered, scaled by 1.6 like the web)
static inline float ix(int x, float v) { return x + 1.6f + v * 1.2f; }
static inline float iy(int y, float v) { return y + 10.0f + v * 1.2f; }

static const float kBoltX[6] = {13, 6, 11, 11, 18, 13};
static const float kBoltY[6] = {2, 14, 14, 22, 10, 10};

void drawGridIcon(int x, int y, bool on) {
  float xs[6], ys[6];
  for (int i = 0; i < 6; i++) {
    xs[i] = ix(x, kBoltX[i]);
    ys[i] = iy(y, kBoltY[i]);
  }
  if (on) {
    fillPolygon(xs, ys, 6, GxEPD_BLACK);
  } else {
    for (int i = 0; i < 6; i++) {
      int j = (i + 1) % 6;
      thickLine((int)xs[i], (int)ys[i], (int)xs[j], (int)ys[j], 2, GxEPD_BLACK);
    }
    thickLine((int)ix(x, 4), (int)iy(y, 4), (int)ix(x, 20), (int)iy(y, 20), 2, GxEPD_BLACK);
  }
}

void drawStatusIcon(int x, int y, uint8_t status) {
  if (status == dash::CHARGE_DISCHARGING) {
    thickLine((int)ix(x, 12), (int)iy(y, 4), (int)ix(x, 12), (int)iy(y, 18), 2, GxEPD_BLACK);
    thickLine((int)ix(x, 6), (int)iy(y, 12), (int)ix(x, 12), (int)iy(y, 20), 2, GxEPD_BLACK);
    thickLine((int)ix(x, 12), (int)iy(y, 20), (int)ix(x, 18), (int)iy(y, 12), 2, GxEPD_BLACK);
  } else if (status == dash::CHARGE_CHARGING) {
    thickLine((int)ix(x, 12), (int)iy(y, 6), (int)ix(x, 12), (int)iy(y, 20), 2, GxEPD_BLACK);
    thickLine((int)ix(x, 6), (int)iy(y, 12), (int)ix(x, 12), (int)iy(y, 4), 2, GxEPD_BLACK);
    thickLine((int)ix(x, 12), (int)iy(y, 4), (int)ix(x, 18), (int)iy(y, 12), 2, GxEPD_BLACK);
  } else {  // idle / unknown: checkmark
    thickLine((int)ix(x, 5), (int)iy(y, 12), (int)ix(x, 10), (int)iy(y, 18), 2, GxEPD_BLACK);
    thickLine((int)ix(x, 10), (int)iy(y, 18), (int)ix(x, 19), (int)iy(y, 6), 2, GxEPD_BLACK);
  }
}

void drawLoadIcon(int x, int y) {
  int cx = (int)ix(x, 12);
  int cy = (int)iy(y, 12);
  int r = (int)(9 * 1.2f);
  g().drawCircle(cx, cy, r, GxEPD_BLACK);
  g().drawCircle(cx, cy, r - 1, GxEPD_BLACK);
  thickLine(cx, cy, cx + (int)(5 * 1.2f), cy - (int)(5 * 1.2f), 2, GxEPD_BLACK);
  g().fillCircle(cx, cy, 3, GxEPD_BLACK);
}

// ---------------------------------------------------------------- outage tile

void drawOutageTile(int x, int y, int w, int h, const dash::HourlyOutage& ho, bool greyMode) {
  bool isFull = ho.fraction >= 1.0f || ho.halfAffected == dash::HALF_BOTH;
  bool isNone = ho.fraction == 0.0f;
  bool isPartial = !isNone && !isFull;

  g().fillRect(x, y, w, h, GxEPD_WHITE);

  if (isFull) {
    if (greyMode) {
      fillRectDither(x, y, w, h);
    } else {
      g().fillRect(x, y, w, h, GxEPD_BLACK);
    }
  } else if (isPartial) {
    // Diagonal from bottom-left to top-right, matching OutageDisplay.tsx
    float xs[3], ys[3];
    if (ho.halfAffected == dash::HALF_FIRST) {  // left triangle
      xs[0] = x; ys[0] = y + h;
      xs[1] = x; ys[1] = y;
      xs[2] = x + w; ys[2] = y;
    } else {  // right triangle
      xs[0] = x; ys[0] = y + h;
      xs[1] = x + w; ys[1] = y;
      xs[2] = x + w; ys[2] = y + h;
    }
    fillPolygon(xs, ys, 3, GxEPD_BLACK, greyMode);
  }

  // Hour label. Full tiles get white text; partial-first puts white text on
  // the dark left side, partial-second black text on the white left side.
  char label[3];
  snprintf(label, sizeof(label), "%02d", ho.hour);

  U8G2_FOR_ADAFRUIT_GFX& f = display::u8g2();
  f.setFont(u8g2_font_helvB08_tf);
  int textW = f.getUTF8Width(label);
  int cx = x + w / 2;
  uint16_t color = GxEPD_BLACK;
  if (isFull) {
    color = greyMode ? GxEPD_BLACK : GxEPD_WHITE;
  } else if (isPartial) {
    cx = x + (int)(w * 0.3f) + 1;
    if (ho.halfAffected == dash::HALF_FIRST && !greyMode) color = GxEPD_WHITE;
  }
  f.setForegroundColor(color);
  f.setCursor(cx - textW / 2, y + h / 2 + 4);
  f.print(label);
  f.setForegroundColor(GxEPD_BLACK);

  // tile border keeps the row readable when adjacent tiles are filled
  g().drawRect(x, y, w + 1, h, GxEPD_BLACK);
}

// ---------------------------------------------------------------- battery graph
// Direct port of BatteryGraph.tsx (528x180, padding 16/8/32/36).

void drawBatteryGraph(int x, int y, const dash::BatteryPoint* points, int count) {
  const int width = 528, height = 180;
  const int padTop = 16, padRight = 8, padBottom = 32, padLeft = 36;
  const int graphW = width - padLeft - padRight;
  const int graphH = height - padTop - padBottom;
  const int baselineY = y + padTop + graphH;

  U8G2_FOR_ADAFRUIT_GFX& f = display::u8g2();
  f.setFont(u8g2_font_helvB08_tf);

  // Y labels
  const char* yLabels[3] = {"100%", "50%", "0%"};
  int yPos[3] = {y + padTop + 4, y + padTop + graphH / 2 + 4, y + padTop + graphH + 4};
  for (int i = 0; i < 3; i++) {
    int tw = f.getUTF8Width(yLabels[i]);
    f.setCursor(x + padLeft - 6 - tw, yPos[i]);
    f.print(yLabels[i]);
  }

  // Grid: dashed at 100% and 50%, solid baseline + Y axis
  for (int gx = x + padLeft; gx < x + width - padRight; gx += 10) {
    int seg = std::min(4, x + width - padRight - gx);
    g().drawFastHLine(gx, y + padTop, seg, GxEPD_BLACK);
    g().drawFastHLine(gx, y + padTop + graphH / 2, seg, GxEPD_BLACK);
  }
  g().fillRect(x + padLeft, baselineY, graphW, 2, GxEPD_BLACK);
  g().fillRect(x + padLeft, y + padTop, 2, graphH, GxEPD_BLACK);

  if (count < 2) return;

  auto scaleX = [&](int i) {
    return x + padLeft + (int)lroundf((float)i / (count - 1) * graphW);
  };
  auto scaleY = [&](float v) {
    return y + padTop + graphH - (int)lroundf(v / 100.0f * graphH);
  };

  // X ticks at 3-hour boundaries from actual timestamps (Kyiv local hours)
  int lastLabelHour = -1;
  f.setFont(u8g2_font_6x10_tf);
  for (int i = 0; i < count; i++) {
    time_t t = points[i].epoch;
    struct tm local;
    localtime_r(&t, &local);
    if (local.tm_hour % 3 == 0 && local.tm_hour != lastLabelHour) {
      int tx = scaleX(i);
      g().fillRect(tx, baselineY, 2, 6, GxEPD_BLACK);
      char lbl[3];
      snprintf(lbl, sizeof(lbl), "%02d", local.tm_hour);
      int tw = f.getUTF8Width(lbl);
      f.setCursor(tx - tw / 2, baselineY + 18);
      f.print(lbl);
      lastLabelHour = local.tm_hour;
    }
  }

  // Data polyline, 3px stroke
  for (int i = 0; i + 1 < count; i++) {
    thickLine(scaleX(i), scaleY(points[i].percent), scaleX(i + 1), scaleY(points[i + 1].percent),
              3, GxEPD_BLACK);
  }
}

// ---------------------------------------------------------------- stale badge

void drawStaleBadge(int x, int yCenter) {
  int r = 8;
  g().fillCircle(x + r, yCenter, r, GxEPD_BLACK);
  U8G2_FOR_ADAFRUIT_GFX& f = display::u8g2();
  f.setFont(u8g2_font_helvB10_tf);
  f.setForegroundColor(GxEPD_WHITE);
  int tw = f.getUTF8Width("!");
  f.setCursor(x + r - tw / 2, yCenter + 5);
  f.print("!");
  f.setForegroundColor(GxEPD_BLACK);
}

}  // namespace widgets
