#include "screen.h"

#include <esp_attr.h>
#include <string.h>

#include <type_traits>

#include "../util/crc32.h"
#include "../util/log.h"
#include "display.h"
#include "render_fx.h"

namespace screen {

// Bump when Frame (or anything it contains) changes layout.
static const uint32_t kMagic = 0x46524D01;  // "FRM" v1

struct Snapshot {
  uint32_t magic;
  uint32_t size;
  uint32_t crc;          // over everything below
  uint32_t pushSerial;   // display::pushSerial() right after our push
  uint32_t frameCrc;     // CRC of the rendered frame on the panel
  uint32_t lastFullEpoch;
  uint16_t partials;     // partial refreshes since the last full one
  bool lastWasNight;
  Frame frame;
};

// ~2.4 KB of the 8 KB RTC slow memory. Raw bytes on purpose: Frame has
// default member initializers, and a static object with a constructor would
// be re-initialized on every boot, NOINIT section or not.
static_assert(sizeof(Snapshot) < 4096, "display snapshot must fit RTC RAM");
static_assert(std::is_trivially_copyable<Frame>::value, "Frame is copied as bytes");
RTC_NOINIT_ATTR static uint32_t snapWords[(sizeof(Snapshot) + 3) / 4];
static Snapshot& snap = *reinterpret_cast<Snapshot*>(snapWords);

static uint32_t snapCrc() {
  const uint8_t* start = reinterpret_cast<const uint8_t*>(&snap.pushSerial);
  const uint8_t* end = reinterpret_cast<const uint8_t*>(&snap) + sizeof(Snapshot);
  return crc32_calc(start, end - start);
}

static bool snapValid() {
  return snap.magic == kMagic && snap.size == sizeof(Snapshot) && snap.crc == snapCrc() &&
         snap.pushSerial == display::pushSerial();
}

void invalidate() { snap.magic = 0; }

static void render(const Frame& f) {
  if (f.page == PAGE_FX) {
    render_fx::render(f.hasFx, f.fxStale, f.fx);
  } else {
    render_power::render(f.power);
  }
  render_statusbar::render(f.bar);
}

static void save(const Frame& f, uint32_t frameCrc, uint32_t lastFull, uint16_t partials,
                 bool night) {
  snap.frame = f;
  snap.frameCrc = frameCrc;
  snap.lastFullEpoch = lastFull;
  snap.partials = partials;
  snap.lastWasNight = night;
  snap.pushSerial = display::pushSerial();
  snap.size = sizeof(Snapshot);
  snap.crc = snapCrc();
  snap.magic = kMagic;
}

Result present(const Frame& f, const Policy& p) {
  display::begin();
  render(f);
  uint32_t crc = display::frameCrc();

  bool valid = snapValid();
  if (valid && !p.forceFull && crc == snap.frameCrc) {
    LOGI("screen", "frame unchanged, panel untouched");
    return UNCHANGED;
  }

  const char* why = nullptr;
  if (p.forceFull) {
    why = "forced";
  } else if (!valid) {
    why = "panel state unknown";
  } else if (snap.frame.page != f.page) {
    why = "page change";
  } else if (p.maxPartials && snap.partials >= p.maxPartials) {
    why = "partial limit";
  } else if (p.now && p.fullRefreshMin && !p.night &&
             (p.now < snap.lastFullEpoch || p.now - snap.lastFullEpoch >= p.fullRefreshMin * 60UL)) {
    why = "interval";
  } else if (snap.lastWasNight && !p.night) {
    why = "night ended";
  } else if (p.cold) {
    why = "cold panel";
  }

  if (!why) {
    // Feed the controller the frame that is physically on the panel, then
    // the new one. The redraw must reproduce it bit for bit; if it does not
    // (e.g. a firmware update changed a renderer), fall back to full.
    static Frame prev;  // static: ~2.3 KB, keep it off the stack
    prev = snap.frame;
    display::clear();
    render(prev);
    if (display::frameCrc() == snap.frameCrc) {
      display::pushPrevious();
      display::clear();
      render(f);
      display::pushPartial();
      save(f, crc, snap.lastFullEpoch, snap.partials + 1, p.night);
      return PARTIAL;
    }
    why = "previous frame not reproducible";
    display::clear();
    render(f);
  }

  LOGI("screen", "full refresh: %s", why);
  display::show();
  save(f, crc, p.now, 0, p.night);
  return FULL;
}

}  // namespace screen
