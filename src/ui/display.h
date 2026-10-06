// GxEPD2 wrapper for the 7.5" UC8179 panel (GDEY075T7, 800x480 1-bit).
// Full-height buffer (48 KB) so screens are composed in one pass and pushed
// with a single full refresh per wake. Partial refresh is intentionally not
// used in v1 - one full refresh per wake also prevents ghosting build-up.
#pragma once

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

namespace display {

void begin();                 // SPI + panel init; safe to call once per wake
Adafruit_GFX& gfx();          // draw target (buffer), GxEPD_BLACK/GxEPD_WHITE
U8G2_FOR_ADAFRUIT_GFX& u8g2();  // font renderer bound to gfx()
// Always use this instead of u8g2().setFont(): the library silently resets
// to solid-background mode on every font change, and with its default
// (black) background each glyph renders as a filled black box.
void setFont(const uint8_t* font);
void clear();                 // white background
void show();                  // full refresh: push buffer to the panel (~4-5 s)
void hibernate();             // EPD deep sleep until next reset

constexpr int16_t kWidth = 800;
constexpr int16_t kHeight = 480;

}  // namespace display
