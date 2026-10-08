// Host-side unit tests for air raid alert picking and formatting
// (pio test -e native). Entries mirror the api.ukrainealarm.com v3 sample.
#include <alerts.h>
#include <string.h>
#include <unity.h>

using namespace dash;

void setUp() {}
void tearDown() {}

static const char* kDrone = "Дронова загроза (жовтий рівень)";

static AlertEntry entry(uint8_t level, uint8_t type, uint32_t created, const char* reason = "") {
  AlertEntry e;
  memset(&e, 0, sizeof(e));
  e.level = level;
  e.type = type;
  e.createdEpoch = created;
  copyUtf8(e.reason, reason, sizeof(e.reason));
  return e;
}

static void test_parse_iso_utc() {
  uint32_t t = 0;
  TEST_ASSERT_TRUE(parseIsoUtc("1970-01-01T00:00:00Z", t));
  TEST_ASSERT_EQUAL_UINT32(0, t);
  TEST_ASSERT_TRUE(parseIsoUtc("2022-04-04T16:45:00Z", t));
  TEST_ASSERT_EQUAL_UINT32(1649090700, t);
  // variable fraction digits
  TEST_ASSERT_TRUE(parseIsoUtc("2026-10-08T05:29:56.21972Z", t));
  uint32_t t2 = 0;
  TEST_ASSERT_TRUE(parseIsoUtc("2026-10-08T05:29:56.329198Z", t2));
  TEST_ASSERT_EQUAL_UINT32(t, t2);
  TEST_ASSERT_TRUE(parseIsoUtc("2024-02-29T12:00:00Z", t));  // leap day
  TEST_ASSERT_EQUAL_UINT32(1709208000, t);
  TEST_ASSERT_FALSE(parseIsoUtc("", t));
  TEST_ASSERT_FALSE(parseIsoUtc("yesterday", t));
  TEST_ASSERT_FALSE(parseIsoUtc(nullptr, t));
}

static void test_strings() {
  TEST_ASSERT_EQUAL(ALERT_AIR, alertTypeFromString("AIR"));
  TEST_ASSERT_EQUAL(ALERT_ARTILLERY, alertTypeFromString("ARTILLERY"));
  TEST_ASSERT_EQUAL(ALERT_URBAN_FIGHTS, alertTypeFromString("URBAN_FIGHTS"));
  TEST_ASSERT_EQUAL(ALERT_CHEMICAL, alertTypeFromString("CHEMICAL"));
  TEST_ASSERT_EQUAL(ALERT_NUCLEAR, alertTypeFromString("NUCLEAR"));
  TEST_ASSERT_EQUAL(ALERT_OTHER, alertTypeFromString("UNKNOWN"));
  TEST_ASSERT_EQUAL(ALERT_OTHER, alertTypeFromString("INFO"));
  TEST_ASSERT_EQUAL(ALERT_OTHER, alertTypeFromString("CUSTOM"));
  TEST_ASSERT_EQUAL(ALERT_YELLOW, alertLevelFromString("Yellow"));
  TEST_ASSERT_EQUAL(ALERT_RED, alertLevelFromString("Red"));
  TEST_ASSERT_EQUAL(ALERT_RED, alertLevelFromString(""));  // never drop an alert
}

static void test_copy_utf8() {
  char buf[8];
  copyUtf8(buf, "  abc  ", sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("abc", buf);
  // "Дрон" is 8 bytes: 7 fit, but the 4th letter must not be cut in half
  copyUtf8(buf, "Дрон", sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Дро", buf);
  copyUtf8(buf, nullptr, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);
  char big[kAlertReasonMax];
  copyUtf8(big, kDrone, sizeof(big));
  TEST_ASSERT_EQUAL_STRING(kDrone, big);
}

static void test_pick_none() {
  AlertStatus a = pickAlert(nullptr, 0);
  TEST_ASSERT_EQUAL(ALERT_NONE, a.level);
  char buf[128] = "x";
  formatAlert(a, "14:05", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_pick_red_over_yellow_and_air_first() {
  // Pokrovska community: red artillery + yellow air
  AlertEntry e[] = {entry(ALERT_RED, ALERT_ARTILLERY, 1000), entry(ALERT_YELLOW, ALERT_AIR, 500)};
  AlertStatus a = pickAlert(e, 2);
  TEST_ASSERT_EQUAL(ALERT_RED, a.level);
  TEST_ASSERT_EQUAL(ALERT_ARTILLERY, a.type);
  TEST_ASSERT_EQUAL(1, a.extraTypes);
  TEST_ASSERT_EQUAL_UINT32(1000, a.sinceEpoch);

  // same level: air wins, then the earliest
  AlertEntry f[] = {
      entry(ALERT_RED, ALERT_ARTILLERY, 100),
      entry(ALERT_RED, ALERT_AIR, 900),
      entry(ALERT_RED, ALERT_AIR, 800),  // same type from a second region
  };
  a = pickAlert(f, 3);
  TEST_ASSERT_EQUAL(ALERT_AIR, a.type);
  TEST_ASSERT_EQUAL_UINT32(800, a.sinceEpoch);
  TEST_ASSERT_EQUAL(1, a.extraTypes);  // only artillery is "other"
}

static void test_format_uses_api_reason() {
  char buf[128];
  // Vyshhorodskyi district in the sample
  AlertEntry drone[] = {entry(ALERT_YELLOW, ALERT_AIR, 1, kDrone)};
  AlertStatus a = pickAlert(drone, 1);
  TEST_ASSERT_EQUAL_STRING(kDrone, a.reason);
  formatAlert(a, "13:41", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Дронова загроза (жовтий рівень) з 13:41", buf);
}

static void test_format_fallback_names() {
  char buf[128];
  // Red levels come with an empty reason in the sample
  AlertEntry red[] = {entry(ALERT_RED, ALERT_AIR, 1)};
  formatAlert(pickAlert(red, 1), "14:05", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("ПОВІТРЯНА ТРИВОГА з 14:05", buf);

  AlertEntry yellow[] = {entry(ALERT_YELLOW, ALERT_AIR, 1)};
  formatAlert(pickAlert(yellow, 1), "", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Повітряна загроза (жовтий рівень)", buf);

  // Vovchanska: red artillery + red urban fights
  AlertEntry two[] = {entry(ALERT_RED, ALERT_ARTILLERY, 1), entry(ALERT_RED, ALERT_URBAN_FIGHTS, 2)};
  formatAlert(pickAlert(two, 2), "09:31", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("ЗАГРОЗА АРТОБСТРІЛУ з 09:31 +1", buf);

  // the longest reason plus suffixes still fits the status bar buffer
  AlertEntry longOne[] = {entry(ALERT_RED, ALERT_AIR, 1,
                                "Дуже довге пояснення причини тривоги, яке не влазить у рядок "
                                "статусу ніяк")};
  AlertEntry other = entry(ALERT_RED, ALERT_ARTILLERY, 2);
  AlertEntry both[] = {longOne[0], other};
  formatAlert(pickAlert(both, 2), "08.10", buf, sizeof(buf));
  TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, " з 08.10 +1"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_iso_utc);
  RUN_TEST(test_strings);
  RUN_TEST(test_copy_utf8);
  RUN_TEST(test_pick_none);
  RUN_TEST(test_pick_red_over_yellow_and_air_first);
  RUN_TEST(test_format_uses_api_reason);
  RUN_TEST(test_format_fallback_names);
  return UNITY_END();
}
