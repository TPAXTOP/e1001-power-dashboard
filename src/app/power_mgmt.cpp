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
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }
  return (sum / 8) / 1000.0f * BAT_DIVIDER;
}

bool usbPresent() {
  // SY6974B REG08: PG_STAT is bit 2.
  Wire.beginTransmission(kChargerAddr);
  Wire.write(0x08);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kChargerAddr, (uint8_t)1) != 1) return false;
  uint8_t status = Wire.read();
  return (status & 0x04) != 0;
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
