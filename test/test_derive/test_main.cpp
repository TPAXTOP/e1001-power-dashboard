// Host-side unit tests for lib/dashcore derive logic (pio test -e native).
// Expected values mirror the web app's behavior (lib/data-fetchers.ts etc.).
#include <string.h>

#include <derive.h>
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_slots_empty);
  RUN_TEST(test_slots_full_hours);
  RUN_TEST(test_slots_half_hours);
  RUN_TEST(test_slots_not_planned_ignored);
  RUN_TEST(test_alternating_pattern);
  RUN_TEST(test_emergency_no_slots_pattern);
  RUN_TEST(test_schedule_applies_flag);
  RUN_TEST(test_null_schedule);
  RUN_TEST(test_charging_status);
  RUN_TEST(test_runtime_minutes);
  RUN_TEST(test_downsample);
  RUN_TEST(test_format_runtime);
  RUN_TEST(test_format_power);
  RUN_TEST(test_weather_icons);
  RUN_TEST(test_describe_weather);
  RUN_TEST(test_format_hourly_time);
  return UNITY_END();
}
