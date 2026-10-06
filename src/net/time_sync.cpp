#include "time_sync.h"

#include <RTClib.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "../util/log.h"

namespace time_sync {

static RTC_PCF8563 rtc;
static bool rtcOk = false;

// Sanity floor: any epoch before 2024 means the clock was never set.
static const uint32_t kMinValidEpoch = 1704067200;  // 2024-01-01

void initFromRtc(const Config& cfg) {
  setenv("TZ", cfg.tz.c_str(), 1);
  tzset();

  // Wire.begin() already ran in setup().
  rtcOk = rtc.begin();
  if (!rtcOk) {
    LOGW("time", "PCF8563 not responding");
    return;
  }
  if (rtc.lostPower()) {
    LOGW("time", "PCF8563 lost power, time invalid");
    return;
  }

  // RTC keeps UTC; unixtime() converts directly. (Other firmware - e.g. the
  // factory SenseCraft one - may have left local time in it; the SNTP sync
  // forced on every cold boot overwrites that with UTC.)
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
  // Wait for an actual SNTP reply. Polling timeValid() is not enough: the
  // RTC has usually set a plausible (possibly wrong) time already.
  uint32_t before = nowEpoch();
  uint32_t start = millis();
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  while (millis() - start < 8000) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      uint32_t now = nowEpoch();
      int32_t drift = (int32_t)(now - before) - (int32_t)((millis() - start) / 1000);
      if (rtcOk) {
        // Also clears the PCF8563 "voltage low" flag.
        rtc.adjust(DateTime(now));
      }
      struct tm local;
      time_t t = now;
      localtime_r(&t, &local);
      LOGI("time", "SNTP synced: %04d-%02d-%02d %02d:%02d Kyiv (clock was off by %lds)",
           local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
           (long)drift);
      return true;
    }
    delay(100);
  }
  LOGW("time", "SNTP timed out");
  return false;
}

bool timeValid() { return (uint32_t)time(nullptr) >= kMinValidEpoch; }

uint32_t nowEpoch() { return (uint32_t)time(nullptr); }

}  // namespace time_sync
