#include "rtc_state.h"

#include <esp_attr.h>
#include <string.h>

#include "../util/crc32.h"

namespace rtc_state {

// Bump when RtcState changes: an image from an OTA restart must not read an
// older layout (RTC RAM survives software resets).
static const uint32_t kMagic = 0x52544301;  // "RTC" v1

struct Stored {
  uint32_t magic;
  uint32_t size;
  uint32_t crc;
  RtcState state;
};

// NOINIT: not zeroed by the startup code, so it survives deep sleep.
// RTC_DATA_ATTR would be re-initialized on every boot for types with
// initializers; this one is validated by magic + CRC instead.
RTC_NOINIT_ATTR static Stored stored;

static RtcState current;
static bool loaded = false;

RtcState& get() {
  if (!loaded) {
    loaded = true;
    if (stored.magic == kMagic && stored.size == sizeof(RtcState) &&
        stored.crc == crc32_calc(&stored.state, sizeof(RtcState))) {
      current = stored.state;
    } else {
      memset(&current, 0, sizeof(current));
      current.shownBattery = -1;
    }
  }
  return current;
}

void commit() {
  get();
  stored.state = current;
  stored.size = sizeof(RtcState);
  stored.crc = crc32_calc(&stored.state, sizeof(RtcState));
  stored.magic = kMagic;
}

}  // namespace rtc_state
