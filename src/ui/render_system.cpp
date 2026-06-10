#include "render_system.h"

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>
#include <stdio.h>

#include "display.h"

namespace render_system {

static U8G2_FOR_ADAFRUIT_GFX& f() { return display::u8g2(); }

static void title(const char* text) {
  display::clear();
  f().setFont(u8g2_font_helvB12_tf);
  f().setCursor(60, 80);
  f().print(text);
  display::gfx().fillRect(60, 96, 680, 2, GxEPD_BLACK);
}

static void line(int n, const char* text) {
  f().setFont(u8g2_font_helvB10_tf);
  f().setCursor(60, 140 + n * 36);
  f().print(text);
}

void renderSetup(const char* apSsid, const char* apPass, const char* ip) {
  title("E-PAPER DASHBOARD - FIRST TIME SETUP");
  char buf[96];
  line(0, "1. Connect your phone or laptop to this WiFi network:");
  snprintf(buf, sizeof(buf), "      network: %s     password: %s", apSsid, apPass);
  line(1, buf);
  snprintf(buf, sizeof(buf), "2. Open http://%s in a browser", ip);
  line(2, buf);
  line(3, "3. Enter your WiFi and API credentials, then Save & Reboot");
  line(5, "The dashboard will start automatically after setup.");
}

void renderMaintenance(const char* ip) {
  title("MAINTENANCE MODE");
  char buf[96];
  snprintf(buf, sizeof(buf), "Settings & firmware update: http://%s", ip);
  line(0, buf);
  line(2, "The device returns to normal operation after 10 minutes");
  line(3, "of inactivity, or via 'Save & Reboot' in the portal.");
}

void renderBatteryEmpty(float vbat) {
  title("BATTERY EMPTY");
  char buf[64];
  snprintf(buf, sizeof(buf), "Battery voltage: %.2f V", vbat);
  line(0, buf);
  line(2, "Connect USB-C power, then press any button to restart.");
}

}  // namespace render_system
