#include "display.h"

#include <GxEPD2_BW.h>
#include <SPI.h>

#include "../../include/pins.h"

namespace display {

static SPIClass hspi(HSPI);
static GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT> epd(
    GxEPD2_750_GDEY075T7(EPD_CS_PIN, EPD_DC_PIN, EPD_RES_PIN, EPD_BUSY_PIN));
static U8G2_FOR_ADAFRUIT_GFX fonts;
static bool inited = false;

void begin() {
  if (inited) return;
  hspi.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, -1);
  epd.epd2.selectSPI(hspi, SPISettings(2000000, MSBFIRST, SPI_MODE0));
  epd.init(0);  // no diagnostic serial output from the driver
  epd.setRotation(0);
  fonts.begin(epd);
  fonts.setFontMode(1);  // transparent backgrounds
  fonts.setForegroundColor(GxEPD_BLACK);
  inited = true;
}

Adafruit_GFX& gfx() { return epd; }

U8G2_FOR_ADAFRUIT_GFX& u8g2() { return fonts; }

void clear() {
  epd.setFullWindow();
  epd.fillScreen(GxEPD_WHITE);
}

void show() { epd.display(false); }

void hibernate() { epd.hibernate(); }

}  // namespace display
