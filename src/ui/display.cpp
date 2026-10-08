#include "display.h"

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <esp_attr.h>
#include <esp_random.h>

#include "../../include/pins.h"
#include "../util/crc32.h"
#include "../util/log.h"

namespace display {

static SPIClass hspi(HSPI);
// Bare driver: GxEPD2_BW keeps its frame buffer private, but the partial
// refresh needs the previous frame written separately (writeImageToPrevious).
static GxEPD2_750_GDEY075T7 epd(EPD_CS_PIN, EPD_DC_PIN, EPD_RES_PIN, EPD_BUSY_PIN);
// GFXcanvas1 uses GxEPD2's layout: 1 bit per pixel, MSB first, rows of
// 100 bytes, 1 = white (GxEPD_WHITE is 0xFFFF, i.e. "set").
static GFXcanvas1* canvas = nullptr;
static U8G2_FOR_ADAFRUIT_GFX fonts;
static bool panelUp = false;

RTC_NOINIT_ATTR static uint32_t serialMagic;
RTC_NOINIT_ATTR static uint32_t serial;
static const uint32_t kSerialMagic = 0x50555348;  // "PUSH"

void begin() {
  if (canvas) return;
  canvas = new GFXcanvas1(kWidth, kHeight);
  if (!canvas->getBuffer()) {
    // 48 KB; can only fail when the heap is badly broken. Keep going so the
    // device still sleeps instead of crashing in a loop.
    LOGE("disp", "no memory for the frame buffer");
  }
  fonts.begin(*canvas);
  fonts.setFontMode(1);  // transparent backgrounds
  fonts.setForegroundColor(GxEPD_BLACK);
  // Only used if solid mode ever slips back in; white is the less harmful
  // failure than the library's default 0 (= black boxes).
  fonts.setBackgroundColor(GxEPD_WHITE);
  if (serialMagic != kSerialMagic) {
    serialMagic = kSerialMagic;
    serial = esp_random();  // never matches a snapshot from before power loss
  }
}

Adafruit_GFX& gfx() { return *canvas; }

U8G2_FOR_ADAFRUIT_GFX& u8g2() { return fonts; }

void setFont(const uint8_t* font) {
  fonts.setFont(font);   // resets is_transparent to 0 when the font changes
  fonts.setFontMode(1);
}

void clear() { canvas->fillScreen(GxEPD_WHITE); }

void invertRect(int x, int y, int w, int h) {
  uint8_t* buf = canvas->getBuffer();
  if (!buf) return;
  const int stride = (kWidth + 7) / 8;
  for (int yy = y < 0 ? 0 : y; yy < y + h && yy < kHeight; yy++) {
    for (int xx = x < 0 ? 0 : x; xx < x + w && xx < kWidth; xx++) {
      buf[yy * stride + xx / 8] ^= (uint8_t)(0x80 >> (xx & 7));
    }
  }
}

uint32_t frameCrc() {
  if (!canvas->getBuffer()) return 0;
  return crc32_calc(canvas->getBuffer(), (size_t)kWidth * kHeight / 8);
}

// initial = true: the driver clears both controller buffers and the first
// refresh is a full one (the panel content is unknown).
static void panelInit(bool initial) {
  if (panelUp) return;
  hspi.end();  // the SD log may have left the bus begun with its MISO pin
  // GxEPD2 does digitalWrite() before pinMode() on CS/DC/RST, which core 3.x
  // logs as an error for a pin not yet claimed as GPIO. Claim them first;
  // pull-ups keep CS deselected and RST released until the driver drives them.
  pinMode(EPD_CS_PIN, INPUT_PULLUP);
  pinMode(EPD_DC_PIN, INPUT_PULLUP);
  pinMode(EPD_RES_PIN, INPUT_PULLUP);
  hspi.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, -1);
  epd.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  epd.init(0, initial, 10, false);  // no diagnostic serial output from the driver
  panelUp = true;
}

void show() {
  if (!canvas->getBuffer()) return;
  uint32_t t0 = millis();
  panelInit(true);
  epd.writeImageForFullRefresh(canvas->getBuffer(), 0, 0, kWidth, kHeight);
  epd.refresh(false);
  hibernate();
  serial++;
  LOGI("disp", "full refresh %lu ms", millis() - t0);
}

void pushPrevious() {
  if (!canvas->getBuffer()) return;
  panelInit(false);
  epd.writeImageToPrevious(canvas->getBuffer(), 0, 0, kWidth, kHeight);
}

void pushPartial() {
  if (!canvas->getBuffer()) return;
  uint32_t t0 = millis();
  panelInit(false);
  epd.writeImage(canvas->getBuffer(), 0, 0, kWidth, kHeight);
  epd.refresh(true);
  hibernate();
  serial++;
  LOGI("disp", "partial refresh %lu ms", millis() - t0);
}

void hibernate() {
  if (!panelUp) return;
  epd.hibernate();
  panelUp = false;
}

uint32_t pushSerial() { return serial; }

SPIClass& spi() { return hspi; }

}  // namespace display
