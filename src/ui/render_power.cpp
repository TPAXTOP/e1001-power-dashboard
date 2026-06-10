// Port of app/power/page.tsx + power.css. Layout constants mirror the CSS
// box model: left weather column 240px, right column 560px with the outage
// widget on top and backup power below.
#include "render_power.h"

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>
#include <derive.h>
#include <string.h>
#include <time.h>

#include "display.h"
#include "widgets.h"

namespace render_power {

static Adafruit_GFX& g() { return display::gfx(); }
static U8G2_FOR_ADAFRUIT_GFX& f() { return display::u8g2(); }

// Centered text helper; returns x where the text ends.
static int printCentered(const uint8_t* font, const char* text, int cx, int baseline) {
  f().setFont(font);
  int w = f().getUTF8Width(text);
  f().setCursor(cx - w / 2, baseline);
  f().print(text);
  return cx - w / 2 + w;
}

static int printAt(const uint8_t* font, const char* text, int x, int baseline) {
  f().setFont(font);
  f().setCursor(x, baseline);
  f().print(text);
  return x + f().getUTF8Width(text);
}

static void printRight(const uint8_t* font, const char* text, int xRight, int baseline) {
  f().setFont(font);
  int w = f().getUTF8Width(text);
  f().setCursor(xRight - w, baseline);
  f().print(text);
}

// ---------------------------------------------------------------- weather column

static void renderWeather(const PowerView& v) {
  // column divider (border-right: 2px)
  g().fillRect(238, 0, 2, 480, GxEPD_BLACK);

  // header
  int end = printCentered(u8g2_font_helvB12_tf, "Kyiv, Ukraine", 119, 30);
  if (v.weatherStale) widgets::drawStaleBadge(end + 4, 24);
  g().fillRect(12, 41, 214, 2, GxEPD_BLACK);

  if (!v.hasWeather) {
    printCentered(u8g2_font_helvB10_tf, "No data", 119, 120);
    return;
  }

  const dash::WeatherData& w = v.weather;

  // current conditions
  widgets::drawWeatherIcon(dash::weatherCodeToIcon(w.weatherCode), 96, 56, 48);

  char temp[8];
  snprintf(temp, sizeof(temp), "%d", (int)lroundf(w.temperature));
  f().setFont(u8g2_font_logisoso50_tn);
  int tw = f().getUTF8Width(temp);
  int tx = 119 - (tw + 14) / 2;  // +14 ~ degree mark width
  f().setCursor(tx, 166);
  f().print(temp);
  // degree mark drawn manually: logisoso *_tn has digits only
  g().drawCircle(tx + tw + 8, 122, 5, GxEPD_BLACK);
  g().drawCircle(tx + tw + 8, 122, 4, GxEPD_BLACK);

  printCentered(u8g2_font_helvB10_tf, dash::describeWeather(w.weatherCode), 119, 188);

  char hum[24];
  snprintf(hum, sizeof(hum), "Humidity: %d%%", w.humidity);
  printCentered(u8g2_font_helvB08_tf, hum, 119, 206);

  g().fillRect(12, 220, 214, 2, GxEPD_BLACK);

  // hourly forecast: future hours only, up to 6 (page.tsx filter)
  int shown = 0;
  for (int i = 0; i < w.hourlyCount && shown < 6; i++) {
    const dash::HourlyForecast& h = w.hourly[i];
    if (strcmp(h.time, v.nowLocalIso) <= 0) continue;  // ISO strings compare ok

    int rowY = 232 + shown * 34;
    char hhmm[6];
    dash::formatHourlyTime(h.time, hhmm, sizeof(hhmm));
    printAt(u8g2_font_helvB10_tf, hhmm, 14, rowY + 22);

    widgets::drawWeatherIcon(dash::weatherCodeToIcon(h.weatherCode), 100, rowY + 4, 24);

    char ht[8];
    snprintf(ht, sizeof(ht), "%d\xC2\xB0", (int)lroundf(h.temperature));  // UTF-8 degree
    printRight(u8g2_font_helvB12_tf, ht, 226, rowY + 22);
    shown++;
  }
  if (shown == 0) {
    printAt(u8g2_font_helvB10_tf, "--:--", 14, 254);
    printRight(u8g2_font_helvB12_tf, "--", 226, 254);
  }
}

// ---------------------------------------------------------------- outage widget

static void renderOutage(const PowerView& v) {
  int end = printAt(u8g2_font_helvB10_tf, "POWER OUTAGE", 256, 28);
  if (v.outageStale) widgets::drawStaleBadge(end + 6, 22);

  dash::DayOutages today, tomorrow;
  dash::getHourlyOutages(v.hasOutage ? &v.outage : nullptr, today, tomorrow);

  const int tileW = 22, tileH = 32;  // 24 x 22 = 528 = right column content width

  printAt(u8g2_font_helvB08_tf, "TODAY", 256, 50);
  for (int i = 0; i < 24; i++) {
    widgets::drawOutageTile(256 + i * tileW, 56, tileW, tileH, today.hours[i],
                            !today.scheduleApplies);
  }

  printAt(u8g2_font_helvB08_tf, "TOMORROW", 256, 106);
  for (int i = 0; i < 24; i++) {
    widgets::drawOutageTile(256 + i * tileW, 112, tileW, tileH, tomorrow.hours[i],
                            !tomorrow.scheduleApplies);
  }

  g().fillRect(240, 156, 560, 2, GxEPD_BLACK);  // widget border-bottom
}

// ---------------------------------------------------------------- backup widget

static void renderBackup(const PowerView& v) {
  int end = printAt(u8g2_font_helvB10_tf, "BACKUP POWER SUPPLY", 256, 182);
  if (v.backupStale) widgets::drawStaleBadge(end + 6, 176);

  if (!v.hasBackup) {
    printAt(u8g2_font_helvB10_tf, "No data", 256, 280);
    return;
  }
  const dash::BackupData& b = v.backup;

  // "Updated: DD/MM/YYYY HH:MM" (formatKyivDateTimeForDisplay)
  if (b.lastUpdateEpoch) {
    time_t t = b.lastUpdateEpoch;
    struct tm local;
    localtime_r(&t, &local);
    char ts[40];
    snprintf(ts, sizeof(ts), "Updated: %02d/%02d/%04d %02d:%02d", local.tm_mday,
             local.tm_mon + 1, local.tm_year + 1900, local.tm_hour, local.tm_min);
    printRight(u8g2_font_6x10_tf, ts, 784, 180);
  }

  // metrics row: battery % | grid | charge status | load (page.tsx backup-header)
  const int rowY = 196;  // icons are 32x48 boxes
  const int valBase = rowY + 34;

  widgets::drawBatteryIcon(258, rowY, b.batteryPercent);
  char soc[8];
  snprintf(soc, sizeof(soc), "%d%%", (int)lroundf(b.batteryPercent));
  printAt(u8g2_font_logisoso22_tf, soc, 296, valBase);

  widgets::drawGridIcon(404, rowY, b.gridConnected);
  printAt(u8g2_font_logisoso22_tf, b.gridConnected ? "ON" : "OFF", 440, valBase);

  widgets::drawStatusIcon(536, rowY, b.chargingStatus);
  char statusText[12];
  if (b.chargingStatus == dash::CHARGE_DISCHARGING) {
    if (b.hasRuntime) {
      dash::formatRuntime(b.estimatedRuntimeMin, statusText, sizeof(statusText));
    } else {
      strlcpy(statusText, "Drain", sizeof(statusText));
    }
  } else if (b.chargingStatus == dash::CHARGE_CHARGING) {
    strlcpy(statusText, "Charge", sizeof(statusText));
  } else {
    strlcpy(statusText, "Idle", sizeof(statusText));
  }
  printAt(u8g2_font_logisoso22_tf, statusText, 572, valBase);

  widgets::drawLoadIcon(682, rowY);
  char load[12];
  dash::formatPower(b.hasLoadPower, b.loadPowerWatts, load, sizeof(load));
  printAt(u8g2_font_logisoso22_tf, load, 718, valBase);

  // 24h SOC graph
  widgets::drawBatteryGraph(256, 264, b.history, b.historyCount);
}

// ---------------------------------------------------------------- entry

void render(const PowerView& view) {
  display::clear();
  if (view.widgetWeather) renderWeather(view);
  if (view.widgetOutage) renderOutage(view);
  if (view.widgetBackup) renderBackup(view);
}

}  // namespace render_power
