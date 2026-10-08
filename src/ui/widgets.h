// Drawing primitives and widgets shared by the screen renderers.
// Geometry ports the web app's SVG components 1:1 (OutageDisplay.tsx,
// BatteryGraph.tsx, WeatherIcons.tsx, PowerIcons.tsx). "Grey" from the web
// CSS becomes a 50% checker dither - the panel is strictly 1-bit.
#pragma once

#include <data_model.h>
#include <derive.h>
#include <stdint.h>

namespace widgets {

// --- primitives ---
void thickLine(int x0, int y0, int x1, int y1, int stroke, uint16_t color);
// Scanline even-odd fill, optionally dithered (50% checker).
void fillPolygon(const float* xs, const float* ys, int n, uint16_t color, bool dither = false);
void fillRectDither(int x, int y, int w, int h);

// --- weather icons (square, size px) ---
void drawWeatherIcon(dash::WeatherIcon icon, int x, int y, int size);

// --- small glyph icons (firmware-only, square box of `size` px) ---
void drawUmbrellaIcon(int x, int y, int size);
void drawHouseIcon(int x, int y, int size);
void drawDropIcon(int x, int y, int size);
// Status-bar connectivity, kWifiIconW wide, centered on yCenter. state is a
// dash::Connectivity: arcs (OK), arcs + "!" badge (no internet), arcs with a
// slash (no WiFi).
constexpr int kWifiIconW = 30;
void drawWifiIcon(int x, int yCenter, uint8_t state);
// 24x11 status-bar battery (22x11 body + nub); pct < 0 = unknown (empty).
void drawMiniBattery(int x, int y, int pct);

// --- power metric icons (32x48 box, like the web SVGs) ---
void drawBatteryIcon(int x, int y, float pct);
void drawGridIcon(int x, int y, bool on);
void drawStatusIcon(int x, int y, uint8_t chargingStatus);
void drawLoadIcon(int x, int y);

// --- composite widgets ---
// One outage hour tile at (x,y), w x h. greyMode = schedule not confirmed.
void drawOutageTile(int x, int y, int w, int h, const dash::HourlyOutage& ho, bool greyMode);

// Battery 24h graph, 528x180 at (x,y); labels need TZ set for localtime().
void drawBatteryGraph(int x, int y, const dash::BatteryPoint* points, int count);

// Black circle with white "!" appended after a title; x = title end.
void drawStaleBadge(int x, int yCenter);

// Air raid alert sign: black warning triangle with a white "!",
// kAlertIconW x 20, centered on yCenter.
constexpr int kAlertIconW = 24;
void drawAlertIcon(int x, int yCenter);

}  // namespace widgets
