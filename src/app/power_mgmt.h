#pragma once

#include <stdint.h>

namespace power_mgmt {

enum WakeCause : uint8_t { WAKE_COLD = 0, WAKE_TIMER = 1, WAKE_BUTTON = 2 };

WakeCause wakeCause();
int wakeButtonGpio();          // which button woke us (-1 if none)

float batteryVolts();          // averaged ADC reading through the 1:2 divider
bool usbPresent();             // SY6974B charger PG_STAT ("power good") bit, false if absent
void logI2cDevices();          // cold-boot diagnostic: list responding I2C addresses

// Deep sleep with button wake; seconds==0 disables the timer (button-only).
[[noreturn]] void deepSleep(uint32_t seconds);

}  // namespace power_mgmt
