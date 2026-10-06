// Host-side unit tests for lib/dashcore derive logic (pio test -e native).
// Expected values mirror the web app's behavior (lib/data-fetchers.ts etc.).
#include <string.h>

#include <derive.h>
#include <status.h>
#include <unity.h>

using namespace dash;

void setUp() {}
void tearDown() {}

// --- slotsToHourlyFractions -------------------------------------------------

static void test_slots_empty() {
  HourlyOutage out[24];
  slotsToHourlyFractions(nullptr, 0, out);
  for (int i = 0; i < 24; i++) {
    TEST_ASSERT_EQUAL(i, out[i].hour);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out[i].fraction);
    TEST_ASSERT_EQUAL(HALF_NONE, out[i].halfAffected);
  }
}

static void test_slots_full_hours() {
  // 02:00-05:00 outage -> hours 2,3,4 fully out
  OutageSlot slots[] = {{120, 300, SLOT_DEFINITE}};
  HourlyOutage out[24];
  slotsToHourlyFractions(slots, 1, out);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, out[1].fraction);
  for (int h = 2; h <= 4; h++) {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out[h].fraction);
    TEST_ASSERT_EQUAL(HALF_BOTH, out[h].halfAffected);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, out[5].fraction);
}

static void test_slots_half_hours() {
  // 01:30-03:00: hour1 second half, hour2 full
  OutageSlot slots[] = {{90, 180, SLOT_DEFINITE}};
  HourlyOutage out[24];
  slotsToHourlyFractions(slots, 1, out);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, out[1].fraction);
  TEST_ASSERT_EQUAL(HALF_SECOND, out[1].halfAffected);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, out[2].fraction);
  TEST_ASSERT_EQUAL(HALF_BOTH, out[2].halfAffected);

  // 04:00-04:30: hour4 first half only
  OutageSlot slots2[] = {{240, 270, SLOT_DEFINITE}};
  slotsToHourlyFractions(slots2, 1, out);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, out[4].fraction);
  TEST_ASSERT_EQUAL(HALF_FIRST, out[4].halfAffected);
}

static void test_slots_not_planned_ignored() {
  OutageSlot slots[] = {{0, 1440, SLOT_NOT_PLANNED}};
  HourlyOutage out[24];
  slotsToHourlyFractions(slots, 1, out);
  for (int i = 0; i < 24; i++) TEST_ASSERT_EQUAL_FLOAT(0.0f, out[i].fraction);
}

// --- alternating pattern ------------------------------------------------------

static void test_alternating_pattern() {
  HourlyOutage out[24];
  generateAlternatingPattern(true, out);
  TEST_ASSERT_EQUAL(HALF_FIRST, out[0].halfAffected);
  TEST_ASSERT_EQUAL(HALF_SECOND, out[1].halfAffected);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, out[0].fraction);

  generateAlternatingPattern(false, out);
  TEST_ASSERT_EQUAL(HALF_SECOND, out[0].halfAffected);
  TEST_ASSERT_EQUAL(HALF_FIRST, out[1].halfAffected);
}

// --- getHourlyOutages ----------------------------------------------------------

static void test_emergency_no_slots_pattern() {
  OutageSchedule s;
  memset(&s, 0, sizeof(s));
  s.today.present = true;
  strcpy(s.today.status, "EmergencyShutdowns");
  s.tomorrow.present = true;
  strcpy(s.tomorrow.status, "EmergencyShutdowns");

  DayOutages today, tomorrow;
  getHourlyOutages(&s, today, tomorrow);
  TEST_ASSERT_EQUAL(HALF_FIRST, today.hours[0].halfAffected);    // today starts left
  TEST_ASSERT_EQUAL(HALF_SECOND, tomorrow.hours[0].halfAffected);  // tomorrow starts right
  TEST_ASSERT_FALSE(today.scheduleApplies);
  TEST_ASSERT_FALSE(tomorrow.scheduleApplies);
}

static void test_schedule_applies_flag() {
  OutageSchedule s;
  memset(&s, 0, sizeof(s));
  s.today.present = true;
  strcpy(s.today.status, "ScheduleApplies");
  s.today.slotCount = 1;
  s.today.slots[0] = {600, 660, SLOT_DEFINITE};

  DayOutages today, tomorrow;
  getHourlyOutages(&s, today, tomorrow);
  TEST_ASSERT_TRUE(today.scheduleApplies);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, today.hours[10].fraction);
  TEST_ASSERT_FALSE(tomorrow.scheduleApplies);  // absent day
}

// Real Yasno payload, group 29.1 on 2026-10-06 (after the 1.1-60.1 renumbering):
// 18:00-21:30 definite, the rest NotPlanned.
static void test_real_group_29_1() {
  OutageSchedule s;
  memset(&s, 0, sizeof(s));
  s.today.present = true;
  strcpy(s.today.date, "2026-10-06");
  strcpy(s.today.status, "ScheduleApplies");
  s.today.slotCount = 3;
  s.today.slots[0] = {0, 1080, SLOT_NOT_PLANNED};
  s.today.slots[1] = {1080, 1290, SLOT_DEFINITE};
  s.today.slots[2] = {1290, 1440, SLOT_NOT_PLANNED};
  s.tomorrow.present = true;
  strcpy(s.tomorrow.status, "WaitingForSchedule");
  s.tomorrow.slotCount = 1;
  s.tomorrow.slots[0] = {0, 1440, SLOT_NOT_PLANNED};

  DayOutages today, tomorrow;
  getHourlyOutages(&s, today, tomorrow);
  TEST_ASSERT_TRUE(today.scheduleApplies);
  for (int h = 0; h < 18; h++) TEST_ASSERT_EQUAL_FLOAT(0.0f, today.hours[h].fraction);
  for (int h = 18; h <= 20; h++) {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, today.hours[h].fraction);
    TEST_ASSERT_EQUAL(HALF_BOTH, today.hours[h].halfAffected);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.5f, today.hours[21].fraction);
  TEST_ASSERT_EQUAL(HALF_FIRST, today.hours[21].halfAffected);
  for (int h = 22; h < 24; h++) TEST_ASSERT_EQUAL_FLOAT(0.0f, today.hours[h].fraction);

  TEST_ASSERT_FALSE(tomorrow.scheduleApplies);
  for (int h = 0; h < 24; h++) TEST_ASSERT_EQUAL_FLOAT(0.0f, tomorrow.hours[h].fraction);
}

static void test_null_schedule() {
  DayOutages today, tomorrow;
  getHourlyOutages(nullptr, today, tomorrow);
  TEST_ASSERT_FALSE(today.scheduleApplies);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, today.hours[12].fraction);
}

// --- backup power ---------------------------------------------------------------

static void test_charging_status() {
  TEST_ASSERT_EQUAL(CHARGE_UNKNOWN, chargingStatusFrom(false, 0));
  TEST_ASSERT_EQUAL(CHARGE_DISCHARGING, chargingStatusFrom(true, 100));
  TEST_ASSERT_EQUAL(CHARGE_CHARGING, chargingStatusFrom(true, -350));
  TEST_ASSERT_EQUAL(CHARGE_IDLE, chargingStatusFrom(true, 30));
  TEST_ASSERT_EQUAL(CHARGE_IDLE, chargingStatusFrom(true, -50));
}

static void test_runtime_minutes() {
  // (5120 * 0.9) / 850 * 60 = 325.27 -> 325
  TEST_ASSERT_EQUAL_INT32(325, runtimeMinutes(5120, 90, 850));
  TEST_ASSERT_EQUAL_INT32(-1, runtimeMinutes(5120, 90, 0));
  TEST_ASSERT_EQUAL_INT32(-1, runtimeMinutes(5120, 90, -200));
}

static void test_downsample() {
  BatteryPoint pts[200];
  for (int i = 0; i < 200; i++) pts[i] = {(uint32_t)(1000 + i), (float)i};
  int n = downsampleHistory(pts, 200, 96);
  TEST_ASSERT_EQUAL(96, n);
  TEST_ASSERT_EQUAL_UINT32(1000, pts[0].epoch);       // first kept
  TEST_ASSERT_EQUAL_UINT32(1199, pts[95].epoch);      // newest always kept
  // short series untouched
  n = downsampleHistory(pts, 50, 96);
  TEST_ASSERT_EQUAL(50, n);
}

// --- formatting -------------------------------------------------------------------

static void test_format_runtime() {
  char buf[12];
  formatRuntime(45, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("~45m", buf);
  formatRuntime(150, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("~2h30m", buf);
  formatRuntime(600, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("~10h", buf);
  formatRuntime(120, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("~2h", buf);
  formatRuntime(-1, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("--", buf);
}

static void test_format_power() {
  char buf[12];
  formatPower(true, 850, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("850W", buf);
  formatPower(true, -350, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("350W", buf);
  formatPower(true, 1340, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1.3kW", buf);
  formatPower(false, 0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("--", buf);
}

// --- weather -----------------------------------------------------------------------

static void test_weather_icons() {
  TEST_ASSERT_EQUAL(ICON_SUNNY, weatherCodeToIcon(0));
  TEST_ASSERT_EQUAL(ICON_SUNNY, weatherCodeToIcon(1));
  TEST_ASSERT_EQUAL(ICON_PARTLY_CLOUDY, weatherCodeToIcon(2));
  TEST_ASSERT_EQUAL(ICON_CLOUDY, weatherCodeToIcon(3));
  TEST_ASSERT_EQUAL(ICON_CLOUDY, weatherCodeToIcon(45));
  TEST_ASSERT_EQUAL(ICON_RAIN, weatherCodeToIcon(63));
  TEST_ASSERT_EQUAL(ICON_RAIN, weatherCodeToIcon(81));
  TEST_ASSERT_EQUAL(ICON_CLOUDY, weatherCodeToIcon(75));   // snow -> cloudy
  TEST_ASSERT_EQUAL(ICON_RAIN, weatherCodeToIcon(95));
  TEST_ASSERT_EQUAL(ICON_RAIN, weatherCodeToIcon(123));  // >= 95 is rain (web parity)
  TEST_ASSERT_EQUAL(ICON_CLOUDY, weatherCodeToIcon(49));  // unknown -> cloudy
}

static void test_describe_weather() {
  TEST_ASSERT_EQUAL_STRING("Partly cloudy", describeWeather(2));
  TEST_ASSERT_EQUAL_STRING("Moderate rain", describeWeather(63));
  TEST_ASSERT_EQUAL_STRING("N/A", describeWeather(999));
}

static void test_format_hourly_time() {
  char buf[6];
  formatHourlyTime("2025-12-19T14:00", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("14:00", buf);
  formatHourlyTime("", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("--:--", buf);
  formatHourlyTime(nullptr, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("--:--", buf);
}

// --- device battery (status.h) -------------------------------------------------------

static void test_battery_percent() {
  TEST_ASSERT_EQUAL(-1, batteryPercentFromVolts(0.0f));  // unknown reading
  TEST_ASSERT_EQUAL(0, batteryPercentFromVolts(3.10f));
  TEST_ASSERT_EQUAL(0, batteryPercentFromVolts(3.27f));
  TEST_ASSERT_EQUAL(50, batteryPercentFromVolts(3.84f));
  TEST_ASSERT_EQUAL(100, batteryPercentFromVolts(4.20f));
  TEST_ASSERT_EQUAL(100, batteryPercentFromVolts(4.35f));
  // halfway between 4.02 (80%) and 4.08 (85%)
  int mid = batteryPercentFromVolts(4.05f);
  TEST_ASSERT_TRUE(mid >= 82 && mid <= 83);
  // monotonic over the whole range
  int prev = 0;
  for (float v = 3.2f; v <= 4.25f; v += 0.01f) {
    int p = batteryPercentFromVolts(v);
    TEST_ASSERT_TRUE(p >= prev);
    prev = p;
  }
}

static void test_drain_per_day() {
  TEST_ASSERT_EQUAL(-1, drainPerDay(6 * 3600, 90));         // too early
  TEST_ASSERT_EQUAL(-1, drainPerDay(2 * 86400, -1));        // unknown percent
  TEST_ASSERT_EQUAL(10, drainPerDay(2 * 86400, 80));        // 20% over 2 days
  TEST_ASSERT_EQUAL(4, drainPerDay(12 * 3600, 98));         // 2% over 12h
  TEST_ASSERT_EQUAL(0, drainPerDay(86400, 100));
}

static void test_format_duration() {
  char buf[12];
  formatDuration(45 * 60 + 59, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("45m", buf);
  formatDuration(80 * 60, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1h 20m", buf);
  formatDuration(2 * 3600, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("2h", buf);
  formatDuration(14 * 3600 + 25 * 60, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("14h", buf);
  formatDuration(3 * 86400 + 4 * 3600 + 59 * 60, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("3d 4h", buf);
  formatDuration(3 * 86400 + 10 * 60, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("3d", buf);
  formatDuration(12 * 86400 + 5 * 3600, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("12d", buf);
}

// --- precipitation (status.h) ---------------------------------------------------------

static void test_max_precip_prob() {
  WeatherData w;
  memset(&w, 0, sizeof(w));
  const char* times[5] = {"2026-10-07T13:00", "2026-10-07T14:00", "2026-10-07T15:00",
                          "2026-10-07T16:00", "2026-10-07T17:00"};
  uint8_t probs[5] = {90, 10, kPrecipUnknown, 40, 80};
  for (int i = 0; i < 5; i++) {
    strcpy(w.hourly[i].time, times[i]);
    w.hourly[i].precipProb = probs[i];
  }
  w.hourlyCount = 5;

  // 14:35 -> window 14:00, 15:00, 16:00 (13:00 is past, 17:00 beyond 3h)
  TEST_ASSERT_EQUAL(40, maxPrecipProb(w, "2026-10-07T14:35", 3));
  TEST_ASSERT_EQUAL(80, maxPrecipProb(w, "2026-10-07T14:35", 4));
  // no clock -> from the first entry
  TEST_ASSERT_EQUAL(90, maxPrecipProb(w, "", 3));
  // window holding only unknowns
  TEST_ASSERT_EQUAL(-1, maxPrecipProb(w, "2026-10-07T15:10", 1));
  // everything in the past
  TEST_ASSERT_EQUAL(-1, maxPrecipProb(w, "2026-10-07T18:00", 3));
}

// --- outage countdown (status.h) -------------------------------------------------------

static void makeDay(OutageDay& d, const char* status, int a, int b) {
  d.present = true;
  strcpy(d.status, status);
  d.slotCount = 3;
  d.slots[0] = {0, (uint16_t)a, SLOT_NOT_PLANNED};
  d.slots[1] = {(uint16_t)a, (uint16_t)b, SLOT_DEFINITE};
  d.slots[2] = {(uint16_t)b, 1440, SLOT_NOT_PLANNED};
}

static void test_outage_status() {
  OutageSchedule s;
  memset(&s, 0, sizeof(s));
  makeDay(s.today, "ScheduleApplies", 1080, 1290);  // 18:00-21:30
  char buf[64];

  TEST_ASSERT_TRUE(formatOutageStatus(s, 16 * 60 + 40, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Next outage 18:00-21:30 \xC2\xB7 in 1h 20m", buf);

  TEST_ASSERT_TRUE(formatOutageStatus(s, 19 * 60, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Power off now \xC2\xB7 back at 21:30", buf);

  // after today's outage, tomorrow not confirmed -> nothing to say
  makeDay(s.tomorrow, "WaitingForSchedule", 480, 690);
  TEST_ASSERT_FALSE(formatOutageStatus(s, 22 * 60, buf, sizeof(buf)));

  // tomorrow confirmed
  strcpy(s.tomorrow.status, "ScheduleApplies");
  TEST_ASSERT_TRUE(formatOutageStatus(s, 22 * 60, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Outage tomorrow 08:00-11:30", buf);
}

static void test_outage_status_across_midnight() {
  OutageSchedule s;
  memset(&s, 0, sizeof(s));
  makeDay(s.today, "ScheduleApplies", 1320, 1440);  // 22:00-24:00
  s.today.slotCount = 2;                            // no trailing NotPlanned slot
  makeDay(s.tomorrow, "ScheduleApplies", 0, 150);   // 00:00-02:30
  char buf[64];

  TEST_ASSERT_TRUE(formatOutageStatus(s, 23 * 60, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Power off now \xC2\xB7 back tomorrow 02:30", buf);

  TEST_ASSERT_TRUE(formatOutageStatus(s, 20 * 60, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Next outage 22:00-02:30 \xC2\xB7 in 2h", buf);
}

static void test_outage_status_emergency() {
  OutageSchedule s;
  memset(&s, 0, sizeof(s));
  s.today.present = true;
  strcpy(s.today.status, "EmergencyShutdowns");
  char buf[64];
  TEST_ASSERT_TRUE(formatOutageStatus(s, 600, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Emergency outages, schedule suspended", buf);

  memset(&s, 0, sizeof(s));  // no data at all
  TEST_ASSERT_FALSE(formatOutageStatus(s, 600, buf, sizeof(buf)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_slots_empty);
  RUN_TEST(test_slots_full_hours);
  RUN_TEST(test_slots_half_hours);
  RUN_TEST(test_slots_not_planned_ignored);
  RUN_TEST(test_alternating_pattern);
  RUN_TEST(test_emergency_no_slots_pattern);
  RUN_TEST(test_schedule_applies_flag);
  RUN_TEST(test_real_group_29_1);
  RUN_TEST(test_null_schedule);
  RUN_TEST(test_charging_status);
  RUN_TEST(test_runtime_minutes);
  RUN_TEST(test_downsample);
  RUN_TEST(test_format_runtime);
  RUN_TEST(test_format_power);
  RUN_TEST(test_weather_icons);
  RUN_TEST(test_describe_weather);
  RUN_TEST(test_format_hourly_time);
  RUN_TEST(test_battery_percent);
  RUN_TEST(test_drain_per_day);
  RUN_TEST(test_format_duration);
  RUN_TEST(test_max_precip_prob);
  RUN_TEST(test_outage_status);
  RUN_TEST(test_outage_status_across_midnight);
  RUN_TEST(test_outage_status_emergency);
  return UNITY_END();
}
