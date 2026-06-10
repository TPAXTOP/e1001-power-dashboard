#pragma once

#include <stddef.h>
#include <stdint.h>

// Standard CRC-32 (reflected, poly 0xEDB88320), bitwise - used only on small
// cache blobs at wake time, so speed is irrelevant.
inline uint32_t crc32_calc(const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}
