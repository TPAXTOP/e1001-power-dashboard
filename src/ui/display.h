// 7.5" UC8179 panel (GDEY075T7, 800x480 1-bit) driven through GxEPD2's bare
// driver, with our own full-frame canvas (48 KB) as the draw target.
//
// Frames are composed in the canvas and pushed either with a full refresh
// (flashes, clears ghosting) or a fast differential partial refresh (~0.5 s,
// only changed pixels move). A partial refresh needs the panel's previous
// image in the controller's "old" RAM; after deep sleep that is restored by
// redrawing the previous frame and pushing it with pushPrevious() first
// (see screen.cpp), so nothing depends on the controller keeping its RAM.
#pragma once

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

namespace display {

void begin();                 // canvas + fonts; the panel is only touched by push*()
Adafruit_GFX& gfx();          // draw target (canvas), GxEPD_BLACK/GxEPD_WHITE
U8G2_FOR_ADAFRUIT_GFX& u8g2();  // font renderer bound to gfx()
// Always use this instead of u8g2().setFont(): the library silently resets
// to solid-background mode on every font change, and with its default
// (black) background each glyph renders as a filled black box.
void setFont(const uint8_t* font);
void clear();                 // white background
void invertRect(int x, int y, int w, int h);  // flips black <-> white
uint32_t frameCrc();          // CRC-32 of the canvas: "would the screen change?"

// Full refresh of the canvas (~1.2-4 s, flashes), then panel deep sleep.
void show();
// Differential refresh: call pushPrevious() with the frame that is on the
// panel now, redraw the new frame, then pushPartial().
void pushPrevious();
void pushPartial();
void hibernate();             // panel deep sleep (show/pushPartial already do this)

// Increments on every push and survives deep sleep (RTC RAM), so a saved
// frame snapshot can tell whether something else was drawn since.
uint32_t pushSerial();

constexpr int16_t kWidth = 800;
constexpr int16_t kHeight = 480;

}  // namespace display
