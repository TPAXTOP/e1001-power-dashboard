// Onboard SHT4x temperature/humidity sensor (I2C 0x44). Minimal driver, no
// library: one high-precision measurement per call.
#pragma once

namespace sht4x {

// Blocking ~10 ms. false when the sensor does not answer or the CRC fails.
// Wire must already be started.
bool read(float& tempC, float& humidity);

}  // namespace sht4x
