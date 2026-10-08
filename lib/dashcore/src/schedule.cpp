#include "schedule.h"

namespace dash {

bool isNight(int minOfDay, int startMin, int endMin) {
  if (startMin == endMin) return false;
  if (startMin < endMin) return minOfDay >= startMin && minOfDay < endMin;
  return minOfDay >= startMin || minOfDay < endMin;  // wraps midnight
}

uint32_t effectiveInterval(uint32_t baseS, bool online, const CadenceRules& r) {
  if (baseS == 0) return 0;
  uint32_t s = baseS;
  if (r.night && r.nightIntervalS > s) s = r.nightIntervalS;
  if (r.lowBattery) s *= 4;
  if (online && r.wifiFails >= 3) s *= 2;
  return s;
}

uint32_t taskDueAt(uint32_t lastSuccess, uint32_t lastAttempt, uint32_t intervalS, uint32_t now) {
  if (intervalS == 0) return kNever;
  // A timestamp in the future means the clock was wrong when it was taken.
  if (lastSuccess > now) lastSuccess = 0;
  if (lastAttempt > now) lastAttempt = 0;
  uint32_t due = 0;
  if (lastSuccess) due = lastSuccess + intervalS;
  if (lastAttempt && lastAttempt + intervalS > due) due = lastAttempt + intervalS;
  return due ? due : now;
}

uint32_t wakeTick(const uint32_t* intervalsS, int n) {
  uint32_t tick = kMaxTickS;
  for (int i = 0; i < n; i++) {
    if (intervalsS[i] && intervalsS[i] < tick) tick = intervalsS[i];
  }
  return tick < kMinTickS ? kMinTickS : tick;
}

uint32_t gridSlot(uint32_t now, uint32_t tick) {
  if (tick == 0) return now;
  return (uint32_t)(((uint64_t)now + tick / 2) / tick * tick);
}

bool dueOnSlot(uint32_t dueAt, uint32_t slot, uint32_t tick) {
  if (dueAt == kNever) return false;
  return dueAt <= slot + tick / 2;
}

bool isStale(uint32_t lastSuccess, uint32_t intervalS, uint32_t now) {
  if (lastSuccess == 0 || intervalS == 0) return false;
  if (lastSuccess > now) return true;  // clock was wrong: treat as unknown age
  return now - lastSuccess > 2 * intervalS + 60;
}

uint32_t sleepSeconds(uint32_t wakeAt, uint32_t now, uint32_t minS, uint32_t maxS) {
  uint32_t s = wakeAt > now ? wakeAt - now : 0;
  if (s < minS) s = minS;
  if (s > maxS) s = maxS;
  return s;
}

}  // namespace dash
