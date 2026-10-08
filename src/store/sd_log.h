// Diagnostic log on the microSD card (optional: without a card nothing
// happens). Log lines are collected in RAM during a wake and written in one
// go right before deep sleep or a restart, so the card is powered for a
// fraction of a second per wake and never while the device sleeps.
//
// Files (Kyiv dates):
//   /log/YYYY-MM-DD.log     every log line, with its local time
//   /log/wakes-YYYY-MM.csv  one row per wake (battery, radio, screen), for
//                           comparing battery life between firmware versions
// When the card is over 85 % full, the oldest day files are deleted.
#pragma once

#include <Arduino.h>
#include <FS.h>

namespace sd_log {

enum State : uint8_t {
  SD_NONE = 0,   // no card in the slot
  SD_OK = 1,     // card in the slot; the last write worked (or none was tried yet)
  SD_ERROR = 2,  // card in the slot, but it could not be mounted or written
};

// One log line (called by logLine() in util/log.h).
void append(char level, const char* tag, const char* msg);

// This wake's CSV row; flush() appends the awake time (ms) as the last
// column. The header is written when a monthly file is created.
void setWakeRow(const char* header, const char* row);

// The card-detect switch (no card power needed).
bool cardPresent();

// Card detect + the result of the last write (kept across deep sleep).
State state();

// Writes the buffered lines and the wake row. Without a card: drops them.
State flush();

// Before the panel uses the shared SCK/MOSI: an inserted but unpowered card
// clamps them through its protection diodes (to its dead supply), and the
// panel then receives noise. Powers an inserted card, deselected, until
// prepareSleep(). No-op without a card.
void powerForBus();

// Before deep sleep: card power off and held off while sleeping.
void prepareSleep();

// Portal: mount (stays mounted until flush()/unmount()), then use fs().
bool mount();
void unmount();
fs::FS& fs();
extern const char* const kDir;  // "/log"

}  // namespace sd_log
