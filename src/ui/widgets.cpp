#include "widgets.h"

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>
#include <math.h>
#include <time.h>

#include <algorithm>

#include "display.h"
#include <status.h>

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

// ---------------------------------------------------------------- glyph icons

void drawUmbrellaIcon(int x, int y, int size) {
  int cx = x + size / 2;
  int r = size / 2;
  int cy = y + r;  // canopy: upper half disc
  for (int dy = -r; dy <= 0; dy++) {
    int hw = (int)lroundf(sqrtf((float)(r * r - dy * dy)));
    g().drawFastHLine(cx - hw, cy + dy, 2 * hw + 1, GxEPD_BLACK);
  }
  int stroke = size >= 14 ? 2 : 1;
  int bottom = y + size - 1;
  g().fillRect(cx, cy, stroke, bottom - cy, GxEPD_BLACK);  // shaft
  int hook = std::max(2, size / 5);
  g().fillRect(cx - hook, bottom - stroke + 1, hook + stroke, stroke, GxEPD_BLACK);
  g().fillRect(cx - hook, bottom - hook, stroke, hook, GxEPD_BLACK);
}

void drawHouseIcon(int x, int y, int size) {
  int roofBase = y + size * 9 / 20;
  g().fillTriangle(x + size / 2, y, x, roofBase, x + size - 1, roofBase, GxEPD_BLACK);
  int bx = x + size * 3 / 20;
  int bw = size - 2 * (size * 3 / 20);
  int bh = y + size - roofBase;
  g().drawRect(bx, roofBase, bw, bh, GxEPD_BLACK);
  g().drawRect(bx + 1, roofBase, bw - 2, bh - 1, GxEPD_BLACK);
  int dw = std::max(3, size / 5);
  int dh = bh * 3 / 5;
  g().fillRect(x + size / 2 - dw / 2, y + size - dh, dw, dh, GxEPD_BLACK);  // door
}

void drawDropIcon(int x, int y, int size) {
  int cx = x + size / 2;
  int r = size * 3 / 10;
  int cy = y + size - 1 - r;
  g().fillCircle(cx, cy, r, GxEPD_BLACK);
  g().fillTriangle(cx, y, cx - r, cy, cx + r, cy, GxEPD_BLACK);
}

void drawWifiIcon(int x, int yCenter, uint8_t state) {
  // Three 90-degree arcs (2 px) over a dot, 18x14, apex at the bottom.
  const int cx = x + 9, cy = yCenter + 7;
  const float kPi = 3.14159265f;
  for (int r = 5; r <= 13; r += 4) {
    for (int a = 0; a <= 32; a++) {
      float ang = kPi * (1.25f + 0.5f * a / 32.0f);  // 225..315 deg, up
      int px = cx + (int)lroundf(cosf(ang) * r);
      int py = cy + (int)lroundf(sinf(ang) * r);
      g().fillRect(px, py, 2, 2, GxEPD_BLACK);
    }
  }
  g().fillCircle(cx, cy - 1, 2, GxEPD_BLACK);

  if (state == dash::CONN_NO_WIFI) {
    // White-edged slash so it stays readable across the arcs.
    thickLine(x + 1, yCenter - 7, x + 18, yCenter + 8, 5, GxEPD_WHITE);
    thickLine(x + 1, yCenter - 7, x + 18, yCenter + 8, 2, GxEPD_BLACK);
  } else if (state == dash::CONN_NO_INTERNET) {
    // small "!" badge to the right, like the stale-data badge
    int bx = x + 23, r = 6;
    g().fillCircle(bx, yCenter, r, GxEPD_BLACK);
    g().fillRect(bx - 1, yCenter - 4, 2, 5, GxEPD_WHITE);
    g().fillRect(bx - 1, yCenter + 2, 2, 2, GxEPD_WHITE);
  }
}

void drawMiniBattery(int x, int y, int pct) {
  g().drawRect(x, y, 22, 11, GxEPD_BLACK);
  g().fillRect(x + 22, y + 3, 2, 5, GxEPD_BLACK);
  if (pct > 0) {
    int w = (int)lroundf(std::min(pct, 100) / 100.0f * 18.0f);
    if (w < 1) w = 1;
    g().fillRect(x + 2, y + 2, w, 7, GxEPD_BLACK);
  }
}

// ---------------------------------------------------------------- power icons
// 32x48 box like the web SVGs (PowerIcons.tsx). Firmware departure: the web
// draws grid/status/load at 24x24 inside that box with thin strokes; here
// they all fill the battery's footprint (24 wide, y+2..y+44) with solid
// shapes and 2px outlines so the row reads as one set from a distance.

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

// Round-capped stroke: a disc of radius r swept along the segment.
static void brushLine(int x0, int y0, int x1, int y1, int r, uint16_t color) {
  int steps = std::max(abs(x1 - x0), abs(y1 - y0));
  for (int i = 0; i <= steps; i++) {
    float t = steps ? (float)i / steps : 0.0f;
    g().fillCircle((int)lroundf(x0 + (x1 - x0) * t), (int)lroundf(y0 + (y1 - y0) * t), r, color);
  }
}

// Miter-offset a simple polygon inward by d px (either winding).
static void insetPolygon(const float* xs, const float* ys, int n, float d, float* ox, float* oy) {
  float area = 0;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    area += xs[i] * ys[j] - xs[j] * ys[i];
  }
  float sgn = area > 0 ? 1.0f : -1.0f;
  for (int i = 0; i < n; i++) {
    int p = (i + n - 1) % n, q = (i + 1) % n;
    float ax = xs[i] - xs[p], ay = ys[i] - ys[p];
    float bx = xs[q] - xs[i], by = ys[q] - ys[i];
    float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
    float n1x = -sgn * ay / la, n1y = sgn * ax / la;  // inward normals
    float n2x = -sgn * by / lb, n2y = sgn * bx / lb;
    float k = d / (1.0f + n1x * n2x + n1y * n2y);
    ox[i] = xs[i] + (n1x + n2x) * k;
    oy[i] = ys[i] + (n1y + n2y) * k;
  }
}

// Web bolt polygon (24 viewBox, spans x 6..18, y 2..22), scaled 2.1x to
// 25x42 and centered in the box.
static const float kBoltX[6] = {13, 6, 11, 11, 18, 13};
static const float kBoltY[6] = {2, 14, 14, 22, 10, 10};

void drawGridIcon(int x, int y, bool on) {
  float xs[6], ys[6];
  for (int i = 0; i < 6; i++) {
    xs[i] = x + 16 + (kBoltX[i] - 12) * 2.1f;
    ys[i] = y + 2 + (kBoltY[i] - 2) * 2.1f;
  }
  fillPolygon(xs, ys, 6, GxEPD_BLACK);
  if (!on) {
    float ix[6], iy[6];
    insetPolygon(xs, ys, 6, 2.0f, ix, iy);
    fillPolygon(ix, iy, 6, GxEPD_WHITE);
    // strike-through with a white halo so it stays readable over the outline
    brushLine(x + 3, y + 6, x + 29, y + 42, 2, GxEPD_WHITE);
    brushLine(x + 3, y + 6, x + 29, y + 42, 1, GxEPD_BLACK);
  }
}

void drawStatusIcon(int x, int y, uint8_t status) {
  if (status == dash::CHARGE_DISCHARGING) {  // solid arrow down
    g().fillRect(x + 12, y + 2, 8, 22, GxEPD_BLACK);
    g().fillTriangle(x + 4, y + 23, x + 28, y + 23, x + 16, y + 44, GxEPD_BLACK);
  } else if (status == dash::CHARGE_CHARGING) {  // solid arrow up
    g().fillTriangle(x + 16, y + 2, x + 4, y + 23, x + 28, y + 23, GxEPD_BLACK);
    g().fillRect(x + 12, y + 23, 8, 22, GxEPD_BLACK);
  } else {  // idle / unknown: bold checkmark
    brushLine(x + 5, y + 24, x + 13, y + 33, 3, GxEPD_BLACK);
    brushLine(x + 13, y + 33, x + 27, y + 13, 3, GxEPD_BLACK);
  }
}

// Mains plug: prongs on top (like the battery nub), solid body, cord below.
void drawLoadIcon(int x, int y) {
  g().fillRoundRect(x + 9, y + 2, 4, 9, 1, GxEPD_BLACK);
  g().fillRoundRect(x + 19, y + 2, 4, 9, 1, GxEPD_BLACK);
  g().fillRoundRect(x + 4, y + 10, 24, 22, 4, GxEPD_BLACK);
  g().fillTriangle(x + 8, y + 31, x + 24, y + 31, x + 16, y + 38, GxEPD_BLACK);
  g().fillRect(x + 14, y + 34, 4, 11, GxEPD_BLACK);
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
  display::setFont(u8g2_font_helvB08_tf);
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
  display::setFont(u8g2_font_helvB08_tf);

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
  int lastLabelHour = -1;  // same font as the Y labels (still set)
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
  display::setFont(u8g2_font_helvB10_tf);
  f.setForegroundColor(GxEPD_WHITE);
  int tw = f.getUTF8Width("!");
  f.setCursor(x + r - tw / 2, yCenter + 5);
  f.print("!");
  f.setForegroundColor(GxEPD_BLACK);
}

void drawAlertIcon(int x, int yCenter) {
  const int top = yCenter - 10, bottom = yCenter + 10, cx = x + kAlertIconW / 2;
  g().fillTriangle(cx, top, x, bottom, x + kAlertIconW - 1, bottom, GxEPD_BLACK);
  g().fillRect(cx - 1, top + 7, 3, 7, GxEPD_WHITE);
  g().fillRect(cx - 1, bottom - 4, 3, 2, GxEPD_WHITE);
}

}  // namespace widgets
