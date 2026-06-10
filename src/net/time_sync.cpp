#include "time_sync.h"

#include <RTClib.h>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

#include "../../include/pins.h"
#include "../util/log.h"

namespace time_sync {

static RTC_PCF8563 rtc;
static bool rtcOk = false;

// Sanity floor: any epoch before 2024 means the clock was never set.
static const uint32_t kMinValidEpoch = 1704067200;  // 2024-01-01

void initFromRtc(const Config& cfg) {
  setenv("TZ", cfg.tz.c_str(), 1);
  tzset();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  rtcOk = rtc.begin();
  if (!rtcOk) {
    LOGW("time", "PCF8563 not responding");
    return;
  }
  if (rtc.lostPower()) {
    LOGW("time", "PCF8563 lost power, time invalid");
    return;
  }

  // RTC keeps UTC; unixtime() converts directly.
  uint32_t epoch = rtc.now().unixtime();
  if (epoch < kMinValidEpoch) {
    LOGW("time", "PCF8563 time looks unset (%lu)", epoch);
    return;
  }
  struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);

  struct tm local;
  time_t now = epoch;
  localtime_r(&now, &local);
  LOGI("time", "from RTC: %04d-%02d-%02d %02d:%02d local", local.tm_year + 1900,
       local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
}

bool syncSntp(const Config& cfg) {
  configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  uint32_t start = millis();
  while (millis() - start < 8000) {
    if (timeValid()) {
      if (rtcOk) {
        rtc.adjust(DateTime((uint32_t)time(nullptr)));
        // Clear the "voltage low" flag by setting time; PCF8563 VL resets on write.
      }
      LOGI("time", "SNTP synced, epoch=%lu", (unsigned long)time(nullptr));
      return true;
    }
    delay(200);
  }
  LOGW("time", "SNTP timed out");
  return false;
}

bool timeValid() { return (uint32_t)time(nullptr) >= kMinValidEpoch; }

uint32_t nowEpoch() { return (uint32_t)time(nullptr); }

}  // namespace time_sync
