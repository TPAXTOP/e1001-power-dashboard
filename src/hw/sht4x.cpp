#include "sht4x.h"

#include <Arduino.h>
#include <Wire.h>

#include "../util/log.h"

namespace sht4x {

static const uint8_t kAddr = 0x44;
static const uint8_t kMeasureHighPrecision = 0xFD;

// Sensirion CRC-8: poly 0x31, init 0xFF (datasheet example: 0xBEEF -> 0x92).
static uint8_t crc8(const uint8_t* data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool read(float& tempC, float& humidity) {
  Wire.beginTransmission(kAddr);
  Wire.write(kMeasureHighPrecision);
  if (Wire.endTransmission() != 0) {
    LOGW("sht4x", "no answer at 0x%02X", kAddr);
    return false;
  }
  delay(10);  // high-precision measurement: max 8.3 ms

  uint8_t buf[6];
  if (Wire.requestFrom(kAddr, (uint8_t)6) != 6) {
    LOGW("sht4x", "short read");
    return false;
  }
  for (int i = 0; i < 6; i++) buf[i] = Wire.read();
  if (crc8(buf, 2) != buf[2] || crc8(buf + 3, 2) != buf[5]) {
    LOGW("sht4x", "CRC mismatch");
    return false;
  }

  uint16_t rawT = (uint16_t)(buf[0] << 8 | buf[1]);
  uint16_t rawRh = (uint16_t)(buf[3] << 8 | buf[4]);
  tempC = -45.0f + 175.0f * rawT / 65535.0f;
  humidity = constrain(-6.0f + 125.0f * rawRh / 65535.0f, 0.0f, 100.0f);
  return true;
}

}  // namespace sht4x
