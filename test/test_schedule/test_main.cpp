// Host-side unit tests for wake scheduling, SOC history merging and the
// change-avoiding display helpers (pio test -e native).
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

static void test_is_night_empty_window() {
  TEST_ASSERT_FALSE(isNight(200, 180, 180));
  TEST_ASSERT_EQUAL(-1, minutesToNightBoundary(200, 180, 180));
}

static void test_minutes_to_night_boundary() {
  TEST_ASSERT_EQUAL(60, minutesToNightBoundary(120, 180, 480));   // 02:00 -> start 03:00
  TEST_ASSERT_EQUAL(180, minutesToNightBoundary(300, 180, 480));  // 05:00 -> end 08:00
  TEST_ASSERT_EQUAL(1140, minutesToNightBoundary(480, 180, 480)); // 08:00 -> start tomorrow
  TEST_ASSERT_EQUAL(60, minutesToNightBoundary(1320, 1380, 360)); // 22:00 -> 23:00
}

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

static void test_task_due() {
  const uint32_t now = 1800000000;
  // never run -> due now
  TEST_ASSERT_EQUAL_UINT32(now, taskDueAt(0, 0, 180, now));
  // disabled
  TEST_ASSERT_EQUAL_UINT32(kNever, taskDueAt(0, 0, 0, now));
  TEST_ASSERT_FALSE(taskDue(kNever, now, 60));
  // success 2 min ago, 3-min interval -> due in 1 min, which the slack covers
  uint32_t due = taskDueAt(now - 120, now - 120, 180, now);
  TEST_ASSERT_EQUAL_UINT32(now + 60, due);
  TEST_ASSERT_TRUE(taskDue(due, now, 60));
  TEST_ASSERT_FALSE(taskDue(due, now, 30));
  // a failed attempt pushes the retry out by one interval, even when the
  // last success is long ago
  due = taskDueAt(now - 3600, now - 10, 180, now);
  TEST_ASSERT_EQUAL_UINT32(now + 170, due);
  TEST_ASSERT_FALSE(taskDue(due, now, 60));
  TEST_ASSERT_TRUE(taskDue(due, now, 180));  // piggyback window
  // timestamps from a wrong clock (future) are ignored
  TEST_ASSERT_EQUAL_UINT32(now, taskDueAt(now + 5000, 0, 180, now));
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

static void test_next_outage_boundary() {
  OutageSchedule s;
  memset(&s, 0, sizeof(s));
  TEST_ASSERT_EQUAL(-1, nextOutageBoundaryMin(s, 600));

  makeDay(s.today, "ScheduleApplies", 1080, 1290);  // 18:00-21:30
  TEST_ASSERT_EQUAL(1080, nextOutageBoundaryMin(s, 600));
  TEST_ASSERT_EQUAL(1290, nextOutageBoundaryMin(s, 1080));  // at the start: next is the end
  TEST_ASSERT_EQUAL(1290, nextOutageBoundaryMin(s, 1200));
  TEST_ASSERT_EQUAL(-1, nextOutageBoundaryMin(s, 1290));

  makeDay(s.tomorrow, "ScheduleApplies", 480, 690);
  TEST_ASSERT_EQUAL(1440 + 480, nextOutageBoundaryMin(s, 1300));

  strcpy(s.today.status, "EmergencyShutdowns");
  TEST_ASSERT_EQUAL(-1, nextOutageBoundaryMin(s, 600));
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
  RUN_TEST(test_minutes_to_night_boundary);
  RUN_TEST(test_effective_interval);
  RUN_TEST(test_task_due);
  RUN_TEST(test_stale);
  RUN_TEST(test_sleep_seconds);
  RUN_TEST(test_next_outage_boundary);
  RUN_TEST(test_countdown_quantized);
  RUN_TEST(test_quantize_age);
  RUN_TEST(test_sticky_battery);
  RUN_TEST(test_sticky_round);
  RUN_TEST(test_soc_merge_buckets_and_trim);
  RUN_TEST(test_soc_merge_incremental);
  RUN_TEST(test_connectivity);
  return UNITY_END();
}
