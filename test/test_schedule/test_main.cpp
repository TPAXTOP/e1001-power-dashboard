// Host-side unit tests for wake scheduling, SOC history merging, charge
// detection and the change-avoiding display helpers (pio test -e native).
#include <string.h>

#include <schedule.h>
#include <status.h>
#include <unity.h>

using namespace dash;

void setUp() {}
void tearDown() {}

// --- night window ------------------------------------------------------------------

static void test_is_night_plain() {
  // 03:00-08:00
  TEST_ASSERT_FALSE(isNight(2 * 60 + 59, 180, 480));
  TEST_ASSERT_TRUE(isNight(180, 180, 480));
  TEST_ASSERT_TRUE(isNight(7 * 60 + 59, 180, 480));
  TEST_ASSERT_FALSE(isNight(480, 180, 480));
}

static void test_is_night_wraps_midnight() {
  // 23:00-06:00
  TEST_ASSERT_TRUE(isNight(23 * 60, 1380, 360));
  TEST_ASSERT_TRUE(isNight(0, 1380, 360));
  TEST_ASSERT_TRUE(isNight(5 * 60 + 59, 1380, 360));
  TEST_ASSERT_FALSE(isNight(6 * 60, 1380, 360));
  TEST_ASSERT_FALSE(isNight(22 * 60 + 59, 1380, 360));
}

static void test_is_night_empty_window() { TEST_ASSERT_FALSE(isNight(200, 180, 180)); }

// --- cadence -----------------------------------------------------------------------

static void test_effective_interval() {
  CadenceRules r;
  TEST_ASSERT_EQUAL_UINT32(180, effectiveInterval(180, true, r));
  TEST_ASSERT_EQUAL_UINT32(0, effectiveInterval(0, true, r));  // disabled stays disabled

  r.night = true;
  r.nightIntervalS = 900;
  TEST_ASSERT_EQUAL_UINT32(900, effectiveInterval(180, true, r));
  TEST_ASSERT_EQUAL_UINT32(1800, effectiveInterval(1800, true, r));  // never faster at night

  r.night = false;
  r.lowBattery = true;
  TEST_ASSERT_EQUAL_UINT32(720, effectiveInterval(180, true, r));

  r.lowBattery = false;
  r.wifiFails = 3;
  TEST_ASSERT_EQUAL_UINT32(360, effectiveInterval(180, true, r));
  TEST_ASSERT_EQUAL_UINT32(180, effectiveInterval(180, false, r));  // offline tasks unaffected
}

static void test_task_due_at() {
  const uint32_t now = 1800000000;
  // never run -> due now
  TEST_ASSERT_EQUAL_UINT32(now, taskDueAt(0, 0, 180, now));
  // disabled
  TEST_ASSERT_EQUAL_UINT32(kNever, taskDueAt(0, 0, 0, now));
  TEST_ASSERT_FALSE(dueOnSlot(kNever, now, 180));
  // a failed attempt pushes the retry out by one interval, even when the
  // last success is long ago
  TEST_ASSERT_EQUAL_UINT32(now + 170, taskDueAt(now - 3600, now - 10, 180, now));
  // timestamps from a wrong clock (future) are ignored
  TEST_ASSERT_EQUAL_UINT32(now, taskDueAt(now + 5000, 0, 180, now));
}

// --- wake grid -----------------------------------------------------------------------

static void test_wake_tick() {
  // alerts 3 min, inverter 3 min, soc 15, outage 10, weather 30, indoor 3
  uint32_t day[] = {180, 180, 900, 600, 1800, 180};
  TEST_ASSERT_EQUAL_UINT32(180, wakeTick(day, 6));
  // disabled sources (0) do not count
  uint32_t some[] = {0, 0, 900, 600, 1800, 0};
  TEST_ASSERT_EQUAL_UINT32(600, wakeTick(some, 6));
  // nothing enabled -> once an hour
  uint32_t none[] = {0, 0};
  TEST_ASSERT_EQUAL_UINT32(kMaxTickS, wakeTick(none, 2));
  // clamped to [1 min, 1 h]
  uint32_t fast[] = {10, 7200};
  TEST_ASSERT_EQUAL_UINT32(kMinTickS, wakeTick(fast, 2));
  uint32_t slow[] = {7200};
  TEST_ASSERT_EQUAL_UINT32(kMaxTickS, wakeTick(slow, 1));

  // night: everything at least 15 min -> the tick is 15 min
  CadenceRules r;
  r.night = true;
  r.nightIntervalS = 900;
  uint32_t night[6];
  for (int i = 0; i < 6; i++) night[i] = effectiveInterval(day[i], true, r);
  TEST_ASSERT_EQUAL_UINT32(900, wakeTick(night, 6));
}

static void test_grid_slot() {
  const uint32_t t0 = 1800000000 - 1800000000 % 3600;  // a full hour
  TEST_ASSERT_EQUAL_UINT32(t0, gridSlot(t0, 180));
  TEST_ASSERT_EQUAL_UINT32(t0 + 180, gridSlot(t0 + 185, 180));  // woke 5 s late
  TEST_ASSERT_EQUAL_UINT32(t0 + 180, gridSlot(t0 + 171, 180));  // timer 9 s early
  TEST_ASSERT_EQUAL_UINT32(t0, gridSlot(t0 + 89, 180));
  TEST_ASSERT_EQUAL_UINT32(t0 + 180, gridSlot(t0 + 90, 180));
  // a 3-min grid contains the full hour and the half hour (outage edges)
  TEST_ASSERT_EQUAL_UINT32(0, (t0 + 1800) % 180);
  TEST_ASSERT_EQUAL_UINT32(t0 + 900, gridSlot(t0 + 905, 900));
}

static void test_due_on_slot() {
  const uint32_t slot = 1800000000 - 1800000000 % 3600;
  TEST_ASSERT_TRUE(dueOnSlot(slot, slot, 180));
  TEST_ASSERT_TRUE(dueOnSlot(slot - 1000, slot, 180));
  TEST_ASSERT_TRUE(dueOnSlot(slot + 90, slot, 180));    // closer to this slot than the next
  TEST_ASSERT_FALSE(dueOnSlot(slot + 91, slot, 180));
}

// Simulates a day of wakes on the grid: every task runs on the slot nearest
// its due time, all tasks share the wakes, and nothing drifts.
static void test_grid_day_simulation() {
  const uint32_t tick = 180;
  const uint32_t intervals[] = {180, 600, 900, 1800};
  const uint32_t t0 = 1800000000 - 1800000000 % 86400;
  uint32_t last[4] = {0, 0, 0, 0};
  int runs[4] = {0, 0, 0, 0};
  for (uint32_t slot = t0; slot < t0 + 86400; slot += tick) {
    uint32_t now = slot + 5 + (slot / tick) % 7;  // late by 5..11 s, like the RTC timer
    TEST_ASSERT_EQUAL_UINT32(slot, gridSlot(now, tick));
    for (int i = 0; i < 4; i++) {
      uint32_t due = taskDueAt(last[i], last[i], intervals[i], now);
      if (dueOnSlot(due, slot, tick)) {
        if (last[i]) {
          uint32_t gap = slot - last[i];
          // the slot nearest to the interval: within half a tick of it
          TEST_ASSERT_TRUE(gap + tick / 2 >= intervals[i]);
          TEST_ASSERT_TRUE(gap <= intervals[i] + tick / 2);
        }
        last[i] = slot;  // stamped with the slot, as wake_cycle does
        runs[i]++;
      }
    }
  }
  TEST_ASSERT_EQUAL(480, runs[0]);  // every wake
  TEST_ASSERT_EQUAL(160, runs[1]);  // 600 s -> every 3rd slot (9 min)
  TEST_ASSERT_EQUAL(96, runs[2]);   // 900 s -> every 5th slot
  TEST_ASSERT_EQUAL(48, runs[3]);   // 1800 s -> every 10th slot
}

static void test_stale() {
  const uint32_t now = 1800000000;
  TEST_ASSERT_FALSE(isStale(0, 180, now));
  TEST_ASSERT_FALSE(isStale(now - 400, 180, now));  // one missed refresh tolerated
  TEST_ASSERT_TRUE(isStale(now - 421, 180, now));
  TEST_ASSERT_TRUE(isStale(now + 100, 180, now));
}

static void test_sleep_seconds() {
  const uint32_t now = 1800000000;
  TEST_ASSERT_EQUAL_UINT32(60, sleepSeconds(now, now, 60, 3600));
  TEST_ASSERT_EQUAL_UINT32(60, sleepSeconds(now - 100, now, 60, 3600));
  TEST_ASSERT_EQUAL_UINT32(170, sleepSeconds(now + 170, now, 60, 3600));
  TEST_ASSERT_EQUAL_UINT32(3600, sleepSeconds(kNever, now, 60, 3600));
}

// --- outage boundaries and countdown ---------------------------------------------------

static void makeDay(OutageDay& d, const char* status, int a, int b) {
  d.present = true;
  strcpy(d.status, status);
  d.slotCount = 3;
  d.slots[0] = {0, (uint16_t)a, SLOT_NOT_PLANNED};
  d.slots[1] = {(uint16_t)a, (uint16_t)b, SLOT_DEFINITE};
  d.slots[2] = {(uint16_t)b, 1440, SLOT_NOT_PLANNED};
}

static void test_countdown_quantized() {
  OutageSchedule s;
  memset(&s, 0, sizeof(s));
  makeDay(s.today, "ScheduleApplies", 1080, 1290);  // 18:00-21:30
  char buf[64];

  formatOutageStatus(s, 1080 - 73, buf, sizeof(buf));  // 1h 13m -> up to 1h 20m
  TEST_ASSERT_EQUAL_STRING("Next outage 18:00-21:30 \xC2\xB7 in 1h 20m", buf);
  formatOutageStatus(s, 1080 - 79, buf, sizeof(buf));  // same text for the whole step
  TEST_ASSERT_EQUAL_STRING("Next outage 18:00-21:30 \xC2\xB7 in 1h 20m", buf);
  formatOutageStatus(s, 1080 - 42, buf, sizeof(buf));  // within the hour: 5-min steps
  TEST_ASSERT_EQUAL_STRING("Next outage 18:00-21:30 \xC2\xB7 in 45m", buf);
  formatOutageStatus(s, 1080 - 1, buf, sizeof(buf));   // never "in 0m"
  TEST_ASSERT_EQUAL_STRING("Next outage 18:00-21:30 \xC2\xB7 in 5m", buf);
}

// --- change-avoiding display values ----------------------------------------------

static void test_quantize_age() {
  TEST_ASSERT_EQUAL_UINT32(300, quantizeAge(60));
  TEST_ASSERT_EQUAL_UINT32(300, quantizeAge(599));
  TEST_ASSERT_EQUAL_UINT32(1500, quantizeAge(1799));
  TEST_ASSERT_EQUAL_UINT32(3600, quantizeAge(3600 + 1799));
  TEST_ASSERT_EQUAL_UINT32(5400, quantizeAge(5400 + 10));
  TEST_ASSERT_EQUAL_UINT32(36000, quantizeAge(36000 + 3599));
}

static void test_sticky_battery() {
  TEST_ASSERT_EQUAL(-1, stickyBatteryStep(-1, 70));
  TEST_ASSERT_EQUAL(70, stickyBatteryStep(71, -1));
  TEST_ASSERT_EQUAL(75, stickyBatteryStep(73, -1));
  // jitter around the 72.5 boundary keeps the shown step
  TEST_ASSERT_EQUAL(70, stickyBatteryStep(73, 70));
  TEST_ASSERT_EQUAL(75, stickyBatteryStep(72, 75));
  TEST_ASSERT_EQUAL(75, stickyBatteryStep(74, 70));
  TEST_ASSERT_EQUAL(65, stickyBatteryStep(66, 70));
}

static void test_sticky_round() {
  TEST_ASSERT_EQUAL(22, stickyRound(21.6f, 0, false, 0.8f));
  TEST_ASSERT_EQUAL(22, stickyRound(21.4f, 22, true, 0.8f));  // hovering at .5 keeps 22
  TEST_ASSERT_EQUAL(22, stickyRound(22.7f, 22, true, 0.8f));
  TEST_ASSERT_EQUAL(23, stickyRound(22.8f, 22, true, 0.8f));
  TEST_ASSERT_EQUAL(21, stickyRound(21.1f, 22, true, 0.8f));
}

// --- charge detection -------------------------------------------------------------

static void test_charge_session() {
  const uint32_t t0 = 1800000000;
  ChargeState s;
  // discharging, no history: nothing known
  TEST_ASSERT_FALSE(updateCharge(s, 3.85f, t0));
  TEST_ASSERT_FALSE(s.charging);
  TEST_ASSERT_EQUAL_UINT32(0, s.endEpoch);
  TEST_ASSERT_FALSE(updateCharge(s, 3.84f, t0 + 180));
  TEST_ASSERT_FALSE(updateCharge(s, 3.86f, t0 + 360));  // +20 mV: noise-sized, no charge

  // plugged in: constant-current rise
  uint32_t t = t0 + 540;
  float v = 3.90f;
  for (; v < 4.195f; v += 0.02f, t += 180) {
    TEST_ASSERT_TRUE(updateCharge(s, v, t));
    TEST_ASSERT_TRUE(s.charging);
    TEST_ASSERT_EQUAL_UINT32(t, s.endEpoch);
  }
  // constant-voltage phase: flat at the top with ADC jitter
  const float cv[] = {4.200f, 4.195f, 4.205f, 4.198f, 4.201f};
  for (float x : cv) {
    TEST_ASSERT_TRUE(updateCharge(s, x, t));
    TEST_ASSERT_TRUE(s.charging);
    t += 180;
  }
  uint32_t lastCharging = t - 180;

  // unplugged: relaxes 40 mV, then slow discharge - the end stays put
  TEST_ASSERT_FALSE(updateCharge(s, 4.165f, t));
  TEST_ASSERT_FALSE(s.charging);
  for (int i = 0; i < 200; i++) {
    t += 180;
    TEST_ASSERT_FALSE(updateCharge(s, 4.16f - i * 0.0005f, t));
  }
  TEST_ASSERT_EQUAL_UINT32(lastCharging, s.endEpoch);
  TEST_ASSERT_FALSE(s.charging);

  // unknown readings are ignored
  TEST_ASSERT_FALSE(updateCharge(s, 0.0f, t + 180));
  TEST_ASSERT_EQUAL_UINT32(lastCharging, s.endEpoch);
}

static void test_charge_no_false_start() {
  // no history, a rested full cell slowly discharging: never "charging"
  const uint32_t t0 = 1800000000;
  ChargeState s;
  for (int i = 0; i < 500; i++) {
    float jitter = (i % 3 - 1) * 0.008f;  // +-8 mV ADC noise
    TEST_ASSERT_FALSE(updateCharge(s, 4.18f - i * 0.0002f + jitter, t0 + i * 180));
    TEST_ASSERT_FALSE(s.charging);
  }
  TEST_ASSERT_EQUAL_UINT32(0, s.endEpoch);
}

static void test_charge_partial_top_up() {
  const uint32_t t0 = 1800000000;
  ChargeState s;
  s.peakV = 4.20f;
  s.minV = 3.80f;
  s.endEpoch = t0 - 86400;
  TEST_ASSERT_FALSE(updateCharge(s, 3.80f, t0));
  TEST_ASSERT_TRUE(updateCharge(s, 3.89f, t0 + 180));  // +90 mV: a new charge
  TEST_ASSERT_EQUAL_UINT32(t0 + 180, s.endEpoch);
  TEST_ASSERT_TRUE(updateCharge(s, 3.95f, t0 + 360));
  TEST_ASSERT_FALSE(updateCharge(s, 3.92f, t0 + 540));  // unplugged at 3.95
  TEST_ASSERT_EQUAL_UINT32(t0 + 360, s.endEpoch);
}

// --- SOC history -------------------------------------------------------------------

static void test_soc_merge_buckets_and_trim() {
  const uint32_t now = 1800000000 - 1800000000 % kSocBucketS + 600;  // 10 min into a bucket
  SocHistory h;
  memset(&h, 0, sizeof(h));

  // 25 h of 1-min data: only the last 24 h survive, one point per bucket
  static BatteryPoint raw[1600];  // 25 h of 1-min points = 1501
  int n = 0;
  for (uint32_t t = now - 25 * 3600; t <= now; t += 60) raw[n++] = {t, (float)(t % 100)};
  int count = mergeSocHistory(h, raw, n, now);
  TEST_ASSERT_TRUE(count <= kHistoryMax);
  TEST_ASSERT_TRUE(count >= kHistoryMax - 1);
  TEST_ASSERT_EQUAL_UINT32(now, h.points[count - 1].epoch);  // newest kept
  TEST_ASSERT_TRUE(h.points[0].epoch >= now - 86400);
  for (int i = 1; i < count; i++) {
    TEST_ASSERT_TRUE(h.points[i].epoch / kSocBucketS > h.points[i - 1].epoch / kSocBucketS);
  }
}

static void test_soc_merge_incremental() {
  const uint32_t now = 1800000000 - 1800000000 % kSocBucketS;
  SocHistory h;
  memset(&h, 0, sizeof(h));
  BatteryPoint first[] = {{now - 3600, 50}, {now - 2700, 52}, {now - 1800, 54}};
  mergeSocHistory(h, first, 3, now);
  TEST_ASSERT_EQUAL(3, h.count);

  // overlap: same bucket as the last point with a newer value, then a new bucket
  BatteryPoint more[] = {{now - 1800, 55}, {now - 1740, 56}, {now - 600, 60}};
  mergeSocHistory(h, more, 3, now + 60);
  TEST_ASSERT_EQUAL(4, h.count);
  TEST_ASSERT_EQUAL_UINT32(now - 1740, h.points[2].epoch);
  TEST_ASSERT_EQUAL_FLOAT(56, h.points[2].percent);
  TEST_ASSERT_EQUAL_FLOAT(60, h.points[3].percent);

  // nothing new: unchanged
  mergeSocHistory(h, nullptr, 0, now + 60);
  TEST_ASSERT_EQUAL(4, h.count);
}

// --- connectivity --------------------------------------------------------------------

static void test_connectivity() {
  TEST_ASSERT_EQUAL(CONN_NO_WIFI, classifyConnectivity(false, 0, 0, 0, CONN_OK));
  TEST_ASSERT_EQUAL(CONN_OK, classifyConnectivity(true, 3, 1, 2, CONN_NO_INTERNET));
  TEST_ASSERT_EQUAL(CONN_NO_INTERNET, classifyConnectivity(true, 2, 0, 2, CONN_OK));
  // only HTTP-level failures without a response cannot happen; no tries keeps the state
  TEST_ASSERT_EQUAL(CONN_NO_INTERNET, classifyConnectivity(true, 0, 0, 0, CONN_NO_INTERNET));
  TEST_ASSERT_EQUAL(CONN_UNKNOWN, classifyConnectivity(true, 0, 0, 0, CONN_NO_WIFI));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_is_night_plain);
  RUN_TEST(test_is_night_wraps_midnight);
  RUN_TEST(test_is_night_empty_window);
  RUN_TEST(test_effective_interval);
  RUN_TEST(test_task_due_at);
  RUN_TEST(test_wake_tick);
  RUN_TEST(test_grid_slot);
  RUN_TEST(test_due_on_slot);
  RUN_TEST(test_grid_day_simulation);
  RUN_TEST(test_stale);
  RUN_TEST(test_sleep_seconds);
  RUN_TEST(test_charge_session);
  RUN_TEST(test_charge_no_false_start);
  RUN_TEST(test_charge_partial_top_up);
  RUN_TEST(test_countdown_quantized);
  RUN_TEST(test_quantize_age);
  RUN_TEST(test_sticky_battery);
  RUN_TEST(test_sticky_round);
  RUN_TEST(test_soc_merge_buckets_and_trim);
  RUN_TEST(test_soc_merge_incremental);
  RUN_TEST(test_connectivity);
  return UNITY_END();
}
