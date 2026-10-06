// Host-side unit tests for the OTA version comparison (pio test -e native).
#include <semver.h>
#include <unity.h>

using namespace dash;

void setUp() {}
void tearDown() {}

static void test_valid() {
  TEST_ASSERT_TRUE(semverValid("0.3.0"));
  TEST_ASSERT_TRUE(semverValid("10.20.30"));
  TEST_ASSERT_TRUE(semverValid("1.0.0-rc.1"));
  TEST_ASSERT_TRUE(semverValid("1.0.0-alpha-beta.2+build.7"));
  TEST_ASSERT_TRUE(semverValid("1.0.0+20261007"));
}

static void test_invalid() {
  TEST_ASSERT_FALSE(semverValid(nullptr));
  TEST_ASSERT_FALSE(semverValid(""));
  TEST_ASSERT_FALSE(semverValid("1.0"));
  TEST_ASSERT_FALSE(semverValid("v1.0.0"));
  TEST_ASSERT_FALSE(semverValid("1.0.0-"));
  TEST_ASSERT_FALSE(semverValid("1.0.0-rc..1"));
  TEST_ASSERT_FALSE(semverValid("1.0.0+"));
  TEST_ASSERT_FALSE(semverValid("1.0.0 "));
  TEST_ASSERT_FALSE(semverValid("1.0.x"));
  TEST_ASSERT_FALSE(semverValid("1234567890.0.0"));
}

static void test_core_order() {
  TEST_ASSERT_TRUE(semverCompare("0.2.0", "0.3.0") < 0);
  TEST_ASSERT_TRUE(semverCompare("0.10.0", "0.9.9") > 0);
  TEST_ASSERT_TRUE(semverCompare("1.0.0", "0.99.99") > 0);
  TEST_ASSERT_TRUE(semverCompare("0.3.1", "0.3.0") > 0);
  TEST_ASSERT_EQUAL(0, semverCompare("0.3.0", "0.3.0"));
}

static void test_prerelease_order() {
  // Precedence chain from semver.org section 11.
  const char* chain[] = {"1.0.0-alpha",      "1.0.0-alpha.1", "1.0.0-alpha.beta",
                         "1.0.0-beta",       "1.0.0-beta.2",  "1.0.0-beta.11",
                         "1.0.0-rc.1",       "1.0.0"};
  const int n = sizeof(chain) / sizeof(chain[0]);
  for (int i = 0; i + 1 < n; i++) {
    TEST_ASSERT_TRUE_MESSAGE(semverCompare(chain[i], chain[i + 1]) < 0, chain[i]);
    TEST_ASSERT_TRUE_MESSAGE(semverCompare(chain[i + 1], chain[i]) > 0, chain[i + 1]);
  }
  TEST_ASSERT_TRUE(semverCompare("0.3.1-rc.1", "0.3.0") > 0);
}

static void test_build_metadata_ignored() {
  TEST_ASSERT_EQUAL(0, semverCompare("1.0.0+a", "1.0.0+b"));
  TEST_ASSERT_EQUAL(0, semverCompare("1.0.0-rc.1+a", "1.0.0-rc.1"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid);
  RUN_TEST(test_invalid);
  RUN_TEST(test_core_order);
  RUN_TEST(test_prerelease_order);
  RUN_TEST(test_build_metadata_ignored);
  return UNITY_END();
}
