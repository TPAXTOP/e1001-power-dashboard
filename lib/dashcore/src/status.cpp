#include "status.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace dash {

// ---------------------------------------------------------------- device battery

// Typical 1S LiPo open-circuit curve. Read at the start of a wake, before
// WiFi, the cell is close enough to resting for this to hold.
static const float kCurveV[] = {3.27f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f,
                                3.79f, 3.80f, 3.82f, 3.84f, 3.85f, 3.87f, 3.91f,
                                3.95f, 3.98f, 4.02f, 4.08f, 4.11f, 4.15f, 4.20f};
static const int kCurveN = sizeof(kCurveV) / sizeof(kCurveV[0]);  // 0, 5, ... 100 %

int batteryPercentFromVolts(float volts) {
  if (volts <= 0.0f) return -1;
  if (volts <= kCurveV[0]) return 0;
  if (volts >= kCurveV[kCurveN - 1]) return 100;
  for (int i = 1; i < kCurveN; i++) {
    if (volts < kCurveV[i]) {
      float t = (volts - kCurveV[i - 1]) / (kCurveV[i] - kCurveV[i - 1]);
      return (int)lroundf((i - 1 + t) * 5.0f);
    }
  }
  return 100;
}

int drainPerDay(uint32_t sinceFullS, int percentNow) {
  if (percentNow < 0 || sinceFullS < kDrainMinS) return -1;
  int used = 100 - percentNow;
  if (used < 0) used = 0;
  return (int)lroundf(used * 86400.0f / (float)sinceFullS);
}

void formatDuration(uint32_t seconds, char* buf, int bufLen) {
  unsigned minutes = seconds / 60;
  if (minutes < 60) {
    snprintf(buf, bufLen, "%um", minutes);
    return;
  }
  unsigned hours = minutes / 60;
  unsigned remMin = minutes % 60;
  if (hours < 10) {
    if (remMin) {
      snprintf(buf, bufLen, "%uh %um", hours, remMin);
    } else {
      snprintf(buf, bufLen, "%uh", hours);
    }
    return;
  }
  if (hours < 24) {
    snprintf(buf, bufLen, "%uh", hours);
    return;
  }
  unsigned days = hours / 24;
  unsigned remH = hours % 24;
  if (days < 10 && remH) {
    snprintf(buf, bufLen, "%ud %uh", days, remH);
  } else {
    snprintf(buf, bufLen, "%ud", days);
  }
}

// ---------------------------------------------------------------- weather

int maxPrecipProb(const WeatherData& w, const char* nowLocalIso, int hours) {
  // "YYYY-MM-DDTHH:MM" -> "YYYY-MM-DDTHH:00": the entry for the current hour.
  char hourStart[20] = "";
  if (nowLocalIso && strlen(nowLocalIso) >= 16) {
    snprintf(hourStart, sizeof(hourStart), "%.14s00", nowLocalIso);
  }

  int best = -1;
  int taken = 0;
  for (int i = 0; i < w.hourlyCount && taken < hours; i++) {
    const HourlyForecast& h = w.hourly[i];
    if (hourStart[0] && strcmp(h.time, hourStart) < 0) continue;  // ISO strings compare ok
    taken++;
    if (h.precipProb != kPrecipUnknown && (int)h.precipProb > best) best = h.precipProb;
  }
  return best;
}

// ---------------------------------------------------------------- outage countdown

struct Span {
  int start;  // minutes since today's midnight; tomorrow is +1440
  int end;
};

static bool applies(const OutageDay& d) {
  return d.present && strcmp(d.status, "ScheduleApplies") == 0;
}

static int collectSpans(const OutageDay& d, int offset, Span* out, int n, int max) {
  if (!applies(d)) return n;
  for (int i = 0; i < d.slotCount && n < max; i++) {
    if (d.slots[i].type != SLOT_DEFINITE) continue;
    out[n++] = {d.slots[i].startMin + offset, d.slots[i].endMin + offset};
  }
  return n;
}

static void hhmm(int minutes, char* buf, int bufLen) {
  minutes = ((minutes % 1440) + 1440) % 1440;
  snprintf(buf, bufLen, "%02d:%02d", minutes / 60, minutes % 60);
}

bool formatOutageStatus(const OutageSchedule& s, int nowMin, char* buf, int bufLen) {
  if (s.today.present && strcmp(s.today.status, "EmergencyShutdowns") == 0) {
    snprintf(buf, bufLen, "Emergency outages, schedule suspended");
    return true;
  }

  Span spans[2 * kSlotsMax];
  int n = collectSpans(s.today, 0, spans, 0, 2 * kSlotsMax);
  n = collectSpans(s.tomorrow, 1440, spans, n, 2 * kSlotsMax);

  // Sort by start, then merge touching spans (a slot ending 24:00 continues
  // into one starting at tomorrow 00:00).
  for (int i = 1; i < n; i++) {
    Span v = spans[i];
    int j = i - 1;
    while (j >= 0 && spans[j].start > v.start) {
      spans[j + 1] = spans[j];
      j--;
    }
    spans[j + 1] = v;
  }
  int m = 0;
  for (int i = 0; i < n; i++) {
    if (m > 0 && spans[i].start <= spans[m - 1].end) {
      if (spans[i].end > spans[m - 1].end) spans[m - 1].end = spans[i].end;
    } else {
      spans[m++] = spans[i];
    }
  }

  char a[12], b[12];
  for (int i = 0; i < m; i++) {
    const Span& sp = spans[i];
    if (sp.end <= nowMin) continue;
    hhmm(sp.end, b, sizeof(b));
    if (sp.start <= nowMin) {
      snprintf(buf, bufLen, "Power off now \xC2\xB7 back %s %s", sp.end > 1440 ? "tomorrow" : "at",
               b);
      return true;
    }
    hhmm(sp.start, a, sizeof(a));
    if (sp.start < 1440) {
      char in[12];
      formatDuration((uint32_t)(sp.start - nowMin) * 60, in, sizeof(in));
      snprintf(buf, bufLen, "Next outage %s-%s \xC2\xB7 in %s", a, b, in);
    } else {
      snprintf(buf, bufLen, "Outage tomorrow %s-%s", a, b);
    }
    return true;
  }
  return false;
}

}  // namespace dash
