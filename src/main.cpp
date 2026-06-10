// reTerminal E1001 standalone e-paper dashboard.
//
// Boot dispatch:
//   - first boot (no WiFi creds)      -> provisioning portal (SoftAP)
//   - green button held ~1.5 s        -> maintenance portal
//   - white button wake               -> cycle page, then normal wake cycle
//   - timer / cold boot               -> normal wake cycle -> deep sleep
#include <Arduino.h>

#include "../include/defaults.h"
#include "../include/pins.h"
#include "../include/version.h"
#include "app/maintenance.h"
#include "app/power_mgmt.h"
#include "app/wake_cycle.h"
#include "store/config_store.h"
#include "store/state_store.h"
#include "util/log.h"

void setup() {
  Serial.begin(115200);
  LOGI("main", "%s %s", APP_NAME, APP_VERSION);

  pinMode(BTN_GREEN_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(LED_GREEN_PIN, OUTPUT);
  digitalWrite(LED_GREEN_PIN, HIGH);  // LED off (active low)

  Config cfg;
  config_store::load(cfg);

  if (!cfg.hasWifi()) {
    maintenance::run(cfg, true);  // never returns
  }

  power_mgmt::WakeCause cause = power_mgmt::wakeCause();
  int wakeBtn = power_mgmt::wakeButtonGpio();
  bool pageButton = false;

  if (cause == power_mgmt::WAKE_BUTTON) {
    if (wakeBtn == BTN_GREEN_PIN) {
      // Held green = maintenance; short press = immediate refresh.
      uint32_t start = millis();
      bool held = true;
      while (millis() - start < BTN_HOLD_MS) {
        if (digitalRead(BTN_GREEN_PIN) == HIGH) {
          held = false;
          break;
        }
        delay(20);
      }
      if (held) {
        digitalWrite(LED_GREEN_PIN, LOW);  // confirm: LED on while in portal
        maintenance::run(cfg, false);      // never returns
      }
    } else if (wakeBtn == BTN_RIGHT_PIN || wakeBtn == BTN_LEFT_PIN) {
      pageButton = true;
    }
  }

  PersistedState st;
  state_store::load(st);

  while (true) {
    uint32_t sleepS = wake_cycle::run(cfg, st, pageButton);
    pageButton = false;

    // Plugged in + configured to stay awake: idle instead of deep sleep so
    // the next cycle starts instantly and serial stays attached for debugging.
    if (cfg.stayAwakeOnUsb && power_mgmt::usbPresent()) {
      LOGI("main", "USB stay-awake: next cycle in %lus", (unsigned long)sleepS);
      uint32_t until = millis() + sleepS * 1000UL;
      while (millis() < until) {
        if (digitalRead(BTN_RIGHT_PIN) == LOW || digitalRead(BTN_LEFT_PIN) == LOW) {
          pageButton = true;
          break;
        }
        delay(50);
      }
      continue;
    }

    power_mgmt::deepSleep(sleepS);  // never returns
  }
}

void loop() {}
