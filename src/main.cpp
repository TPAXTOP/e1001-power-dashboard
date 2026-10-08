// reTerminal E1001 standalone e-paper dashboard.
//
// Boot dispatch:
//   - first boot (no WiFi creds)      -> provisioning portal (SoftAP)
//   - green button held ~1.5 s        -> maintenance portal
//   - green button short press        -> fetch everything now + full refresh
//   - white button wake               -> cycle page, then normal wake cycle
//   - timer / cold boot               -> normal wake cycle -> deep sleep
// A wake cycle only switches WiFi on when a data source is due; the other
// wakes (indoor sensor, outage start/end, full hour) stay offline.
#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>

#include "../include/defaults.h"
#include "../include/pins.h"
#include "../include/version.h"
#include "app/maintenance.h"
#include "app/power_mgmt.h"
#include "app/wake_cycle.h"
#include "net/ota_pull.h"
#include "store/config_store.h"
#include "store/sd_log.h"
#include "store/state_store.h"
#include "util/log.h"

// Keep a freshly updated image in PENDING_VERIFY instead of letting the core
// accept it at boot: it is confirmed only after a healthy wake (online render)
// or on entering the portal, so a crash or reset before that rolls back.
extern "C" bool verifyRollbackLater() { return true; }

// The whole wake runs in loopTask. The core's 8 KB overflowed in the TLS
// handshake of the update check (ECDSA certificate verify, deep in the
// wake cycle): a crash on every cold boot, which also rolled back every new
// image before it could confirm itself (core dump, Oct 2026). The low-water
// mark is logged before every deep sleep.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "CRASH (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "CRASH (task watchdog)";
    case ESP_RST_WDT: return "CRASH (watchdog)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_USB: return "usb";
    default: return "other";
  }
}

void setup() {
  Serial.begin(115200);
  // First line of every wake in the SD log: the version that ran it, and a
  // crash or brownout of the previous run.
  static const char* const kWakeNames[] = {"cold boot", "timer", "button"};
  LOGI("main", "---- %s %s, %s, reset: %s", APP_NAME, APP_VERSION,
       kWakeNames[power_mgmt::wakeCause()], resetReasonName(esp_reset_reason()));

  pinMode(BTN_GREEN_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(LED_GREEN_PIN, OUTPUT);
  digitalWrite(LED_GREEN_PIN, HIGH);  // LED off (active low)

  // I2C first: the USB/charger probe in the wake cycle runs before the RTC init.
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  Config cfg;
  config_store::load(cfg);

  if (!cfg.hasWifi()) {
    maintenance::run(cfg, true);  // never returns
  }

  power_mgmt::WakeCause cause = power_mgmt::wakeCause();
  int wakeBtn = power_mgmt::wakeButtonGpio();
  wake_cycle::Wake wake;

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
      wake.refreshButton = true;
    } else if (wakeBtn == BTN_RIGHT_PIN || wakeBtn == BTN_LEFT_PIN) {
      wake.pageButton = true;
    }
  }

  PersistedState st;
  state_store::load(st);
  ota_pull::noteBootedVersion(st);
  wake.coldBoot = cause == power_mgmt::WAKE_COLD;
  if (wake.coldBoot) power_mgmt::logI2cDevices();

  int verifyRetries = 0;
  while (true) {
    uint32_t sleepS = wake_cycle::run(cfg, st, wake);
    wake = wake_cycle::Wake();

    // Deep sleep would reset a still-unconfirmed new image into a rollback.
    // A WiFi hiccup should not cost the update, so retry a few times first.
    if (ota_pull::pendingVerify() && verifyRetries++ < 3) {
      LOGW("main", "new image not confirmed yet (offline), retry %d in 60 s", verifyRetries);
      sd_log::flush();  // no deep sleep in between to write it
      delay(60000);
      continue;
    }

    // Plugged in + configured to stay awake: idle instead of deep sleep so
    // the next cycle starts instantly and serial stays attached for debugging.
    if (cfg.stayAwakeOnUsb && power_mgmt::usbPresent()) {
      LOGI("main", "USB stay-awake: next cycle in %lus", (unsigned long)sleepS);
      sd_log::flush();
      uint32_t until = millis() + sleepS * 1000UL;
      while (millis() < until) {
        if (digitalRead(BTN_RIGHT_PIN) == LOW || digitalRead(BTN_LEFT_PIN) == LOW) {
          wake.pageButton = true;
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
