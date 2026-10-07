#pragma once

#include <stddef.h>
#include <stdint.h>

// Standard CRC-32 (reflected, poly 0xEDB88320), half-byte table: small enough
// to need no init, fast enough for the 48 KB frame buffer (a few ms).
inline uint32_t crc32_calc(const void* data, size_t len) {
  static const uint32_t kNibble[16] = {
      0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
      0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
      0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    crc = (crc >> 4) ^ kNibble[crc & 0x0F];
    crc = (crc >> 4) ^ kNibble[crc & 0x0F];
  }
  return ~crc;
}
