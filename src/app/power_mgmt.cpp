#include "power_mgmt.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "../../include/pins.h"
#include "../util/log.h"

namespace power_mgmt {

static const uint8_t kChargerAddr = 0x6A;  // SY6974B (BQ2560x-compatible map)

WakeCause wakeCause() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WAKE_TIMER;
    case ESP_SLEEP_WAKEUP_EXT1:
      return WAKE_BUTTON;
    default:
      return WAKE_COLD;
  }
}

int wakeButtonGpio() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return -1;
  uint64_t mask = esp_sleep_get_ext1_wakeup_status();
  if (mask & (1ULL << BTN_GREEN_PIN)) return BTN_GREEN_PIN;
  if (mask & (1ULL << BTN_RIGHT_PIN)) return BTN_RIGHT_PIN;
  if (mask & (1ULL << BTN_LEFT_PIN)) return BTN_LEFT_PIN;
  return -1;
}

float batteryVolts() {
  // The divider is switched: enable it, let it settle, sample, disable again
  // so it does not drain the cell during deep sleep.
  pinMode(BAT_EN_PIN, OUTPUT);
  digitalWrite(BAT_EN_PIN, HIGH);
  delay(10);
  // Global default: per-pin attenuation fails before the pin's first read.
  analogSetAttenuation(ADC_11db);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }
  digitalWrite(BAT_EN_PIN, LOW);
  float volts = (sum / 8) / 1000.0f * BAT_DIVIDER;

  // A single-cell LiPo is never outside ~2.5-4.5 V. Anything else is a bad
  // reading; report "unknown" (0) so the battery policy is skipped instead of
  // putting a healthy device into button-only "battery empty" sleep.
  if (volts < 2.5f || volts > 4.5f) {
    LOGW("power", "implausible battery reading %.2fV, ignoring", volts);
    return 0.0f;
  }
  return volts;
}

bool usbPresent() {
  // SY6974B REG08: PG_STAT is bit 2. The charger part/address is taken from
  // community notes, not Seeed docs - the raw value is logged for checking.
  // Zero-length probe first: quiet when the chip is absent, whereas a failed
  // register read spams IDF i2c errors (the Wire NG driver only transmits a
  // repeated-start write together with the read).
  Wire.beginTransmission(kChargerAddr);
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(kChargerAddr);
  Wire.write(0x08);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kChargerAddr, (uint8_t)1) != 1) return false;
  uint8_t status = Wire.read();
  LOGI("power", "charger REG08=0x%02X", status);
  return (status & 0x04) != 0;
}

void logI2cDevices() {
  String found;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      char buf[6];
      snprintf(buf, sizeof(buf), " 0x%02X", addr);
      found += buf;
    }
  }
  LOGI("power", "I2C devices:%s", found.length() ? found.c_str() : " none");
}

void deepSleep(uint32_t seconds) {
  if (seconds > 0) {
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  }

  // Buttons are active-low with no external pullups: keep RTC pullups on.
  const gpio_num_t buttons[] = {(gpio_num_t)BTN_GREEN_PIN, (gpio_num_t)BTN_RIGHT_PIN,
                                (gpio_num_t)BTN_LEFT_PIN};
  uint64_t mask = 0;
  for (gpio_num_t pin : buttons) {
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
    mask |= 1ULL << pin;
  }
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

  LOGI("power", "deep sleep for %lus", (unsigned long)seconds);
  Serial.flush();
  esp_deep_sleep_start();
  while (true) {}  // not reached
}

}  // namespace power_mgmt
