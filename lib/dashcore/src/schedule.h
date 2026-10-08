// Wake scheduling: per-source cadences, the night window and the wake grid.
// Firmware-only (no web app counterpart). Pure C++, host-unit-tested.
//
// The device wakes on one grid: every `tick` seconds, where tick is the
// shortest effective interval of all enabled tasks, aligned to multiples of
// tick since the epoch (Kyiv is a whole-hour offset from UTC, so a 3-min tick
// lands on :00, :03, ...). Each wake runs every task that is due by then;
// nothing gets a wake of its own.
#pragma once

#include <stdint.h>

namespace dash {

constexpr uint32_t kNever = 0xFFFFFFFFu;

// Minutes since local midnight. The window [startMin, endMin) may wrap
// midnight (e.g. 23:00-06:00); startMin == endMin means "no night".
bool isNight(int minOfDay, int startMin, int endMin);

struct CadenceRules {
  bool night = false;
  uint32_t nightIntervalS = 0;  // at night every task runs at most this often
  bool lowBattery = false;      // x4
  uint16_t wifiFails = 0;       // >= 3 consecutive: online tasks x2
};

// Interval a task actually runs at. base == 0 stays 0 (task disabled).
uint32_t effectiveInterval(uint32_t baseS, bool online, const CadenceRules& r);

// When a task is next due: after both its last success and its last attempt
// (so a failing source is retried once per interval, not on every wake).
// Never run -> due now. interval 0 -> kNever.
uint32_t taskDueAt(uint32_t lastSuccess, uint32_t lastAttempt, uint32_t intervalS, uint32_t now);

// Wake grid period: the shortest non-zero interval (effective intervals of
// the enabled tasks), clamped to [kMinTickS, kMaxTickS]. kMaxTickS when none.
constexpr uint32_t kMinTickS = 60;
constexpr uint32_t kMaxTickS = 3600;
uint32_t wakeTick(const uint32_t* intervalsS, int n);

// The grid slot this wake belongs to: the nearest multiple of tick, so a
// timer that fires a few seconds early or late still hits its slot.
uint32_t gridSlot(uint32_t now, uint32_t tick);

// A task runs on the slot closest to its due time: due no later than half a
// tick after this slot (the next slot would be further from it).
bool dueOnSlot(uint32_t dueAt, uint32_t slot, uint32_t tick);

// A source counts as stale (shown with "!") once its data is older than two
// intervals plus a minute: one missed refresh is tolerated.
bool isStale(uint32_t lastSuccess, uint32_t intervalS, uint32_t now);

// Seconds to sleep until `wakeAt`, clamped to [minS, maxS].
uint32_t sleepSeconds(uint32_t wakeAt, uint32_t now, uint32_t minS, uint32_t maxS);

}  // namespace dash
