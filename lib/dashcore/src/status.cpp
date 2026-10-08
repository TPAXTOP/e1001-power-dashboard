#include "status.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

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

bool updateCharge(ChargeState& s, float volts, uint32_t now) {
  s.charging = false;
  if (volts <= 0.0f || now == 0) return false;

  bool start = s.minV > 0 && volts >= s.minV + kChargeRiseV;
  if (start) {
    s.peakV = volts;
    s.minV = volts;
  } else if (s.peakV > 0 && volts >= s.peakV - kChargePlateauV) {
    if (volts > s.peakV) s.peakV = volts;
  } else {
    if (s.minV <= 0 || volts < s.minV) s.minV = volts;
    if (s.endEpoch) return false;
    // Never saw a charge: count from here rather than show nothing.
    s.endEpoch = now;
    s.estimated = true;
    return true;
  }
  s.charging = true;
  s.endEpoch = now;
  s.estimated = false;
  return true;
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

uint32_t quantizeAge(uint32_t seconds) {
  if (seconds < 3600) {
    uint32_t q = seconds - seconds % 300;
    return q < 300 ? 300 : q;
  }
  if (seconds < 36000) return seconds - seconds % 1800;
  return seconds - seconds % 3600;
}

int stickyBatteryStep(int rawPercent, int shownStep) {
  if (rawPercent < 0) return -1;
  int step = (rawPercent + 2) / 5 * 5;
  if (shownStep < 0) return step;
  int diff = rawPercent - shownStep;
  if (diff < 0) diff = -diff;
  return diff >= 4 ? step : shownStep;
}

int stickyRound(float raw, int shown, bool hasShown, float threshold) {
  int r = (int)lroundf(raw);
  if (!hasShown) return r;
  return fabsf(raw - (float)shown) >= threshold - 1e-4f ? r : shown;  // float slack
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

// Definite outage spans of today and tomorrow (only days whose schedule
// applies), sorted and with touching spans merged (a slot ending 24:00
// continues into one starting at tomorrow 00:00). Returns the count.
static int mergedSpans(const OutageSchedule& s, Span* spans) {
  int n = collectSpans(s.today, 0, spans, 0, 2 * kSlotsMax);
  n = collectSpans(s.tomorrow, 1440, spans, n, 2 * kSlotsMax);

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
  return m;
}

static bool emergency(const OutageSchedule& s) {
  return s.today.present && strcmp(s.today.status, "EmergencyShutdowns") == 0;
}

// Rounded up: 5-min steps within the hour, 10-min steps beyond.
static uint32_t countdownMinutes(int minutes) {
  int step = minutes <= 60 ? 5 : 10;
  return (uint32_t)((minutes + step - 1) / step * step);
}

bool formatOutageStatus(const OutageSchedule& s, int nowMin, char* buf, int bufLen) {
  if (emergency(s)) {
    snprintf(buf, bufLen, "Emergency outages, schedule suspended");
    return true;
  }

  Span spans[2 * kSlotsMax];
  int m = mergedSpans(s, spans);

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
      formatDuration(countdownMinutes(sp.start - nowMin) * 60, in, sizeof(in));
      snprintf(buf, bufLen, "Next outage %s-%s \xC2\xB7 in %s", a, b, in);
    } else {
      snprintf(buf, bufLen, "Outage tomorrow %s-%s", a, b);
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- SOC history

int mergeSocHistory(SocHistory& hist, const BatteryPoint* fresh, int freshCount, uint32_t now) {
  int existing = hist.count > kHistoryMax ? kHistoryMax : hist.count;
  std::vector<BatteryPoint> all;
  all.reserve(existing + (freshCount > 0 ? freshCount : 0));
  all.insert(all.end(), hist.points, hist.points + existing);
  if (freshCount > 0) all.insert(all.end(), fresh, fresh + freshCount);
  // Stable: for equal epochs the fresh point (inserted later) stays last.
  std::stable_sort(all.begin(), all.end(), [](const BatteryPoint& a, const BatteryPoint& b) {
    return a.epoch < b.epoch;
  });

  uint32_t cutoff = now > 86400 ? now - 86400 : 0;
  size_t out = 0;
  for (size_t i = 0; i < all.size(); i++) {
    const BatteryPoint p = all[i];
    if (p.epoch < cutoff || p.epoch > now + 3600) continue;  // too old / clock nonsense
    if (out > 0 && all[out - 1].epoch / kSocBucketS == p.epoch / kSocBucketS) {
      all[out - 1] = p;  // newest value per bucket
    } else {
      all[out++] = p;
    }
  }
  size_t first = out > (size_t)kHistoryMax ? out - kHistoryMax : 0;
  hist.count = (uint8_t)(out - first);
  for (int i = 0; i < hist.count; i++) hist.points[i] = all[first + i];
  return hist.count;
}

// ---------------------------------------------------------------- connectivity

Connectivity classifyConnectivity(bool wifiUp, int attempts, int responses, int transportErrors,
                                  Connectivity previous) {
  if (!wifiUp) return CONN_NO_WIFI;
  if (responses > 0) return CONN_OK;
  if (attempts > 0 && transportErrors > 0) return CONN_NO_INTERNET;
  return previous == CONN_NO_WIFI ? CONN_UNKNOWN : previous;
}

}  // namespace dash
