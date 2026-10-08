#include "sd_log.h"

#include <SD.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "../../include/pins.h"
#include "../ui/display.h"

namespace sd_log {

const char* const kDir = "/log";

// A wake logs a few KB (online, with request details); the portal flushes
// every minute. Lines beyond this are counted and dropped.
static const size_t kBufSize = 32 * 1024;
static const size_t kBufSizeNoPsram = 8 * 1024;
static const uint32_t kSdHz = 4000000;  // same clock as the panel on the shared lines
static const int kPrunePercent = 85;

// Buffer records: header + "<level> <tag> <msg>" (not terminated).
struct RecHeader {
  uint32_t epoch;   // wall clock at log time (0 = clock not set yet)
  uint16_t millis;  // sub-second part of epoch
  uint16_t len;     // text bytes that follow
  uint32_t uptime;  // millis() at log time, for lines before the clock is set
};

static uint8_t* buf = nullptr;
static size_t bufCap = 0;
static size_t used = 0;
static uint32_t dropped = 0;
static bool flushing = false;
static bool mounted = false;
static const char* rowHeader = nullptr;
static char row[256] = "";

// The last write result survives deep sleep (status bar icon).
RTC_NOINIT_ATTR static uint32_t storedMagic;
RTC_NOINIT_ATTR static uint8_t storedState;
static const uint32_t kStateMagic = 0x53444C31;  // "SDL1"

static void setStored(State s) {
  storedState = s;
  storedMagic = kStateMagic;
}

static bool ensureBuffer() {
  if (buf) return true;
  buf = (uint8_t*)heap_caps_malloc(kBufSize, MALLOC_CAP_SPIRAM);
  bufCap = kBufSize;
  if (!buf) {
    buf = (uint8_t*)malloc(kBufSizeNoPsram);
    bufCap = kBufSizeNoPsram;
  }
  return buf != nullptr;
}

void append(char level, const char* tag, const char* msg) {
  if (flushing || !ensureBuffer()) return;
  char text[360];
  int n = snprintf(text, sizeof(text), "%c %-8s %s", level, tag, msg);
  if (n < 0) return;
  if ((size_t)n >= sizeof(text)) n = sizeof(text) - 1;
  if (used + sizeof(RecHeader) + n > bufCap) {
    dropped++;
    return;
  }
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  RecHeader h;
  h.epoch = tv.tv_sec > 1600000000 ? (uint32_t)tv.tv_sec : 0;
  h.millis = (uint16_t)(tv.tv_usec / 1000);
  h.len = (uint16_t)n;
  h.uptime = millis();
  memcpy(buf + used, &h, sizeof(h));
  memcpy(buf + used + sizeof(h), text, n);
  used += sizeof(h) + n;
}

void setWakeRow(const char* header, const char* r) {
  rowHeader = header;
  strlcpy(row, r, sizeof(row));
}

bool cardPresent() {
  // The pullup only while reading: with a card in, it would draw ~70 uA
  // through the detect switch for the whole deep sleep.
  pinMode(SD_DET_PIN, INPUT_PULLUP);
  delayMicroseconds(100);
  bool present = digitalRead(SD_DET_PIN) == LOW;
  pinMode(SD_DET_PIN, INPUT);
  return present;
}

State state() {
  if (!cardPresent()) return SD_NONE;
  if (storedMagic == kStateMagic && storedState == SD_ERROR) return SD_ERROR;
  return SD_OK;
}

static void powerOff() {
  pinMode(SD_CS_PIN, INPUT);  // an unpowered card must not be fed through CS
  pinMode(SD_MISO_PIN, INPUT);
  pinMode(SD_EN_PIN, OUTPUT);
  digitalWrite(SD_EN_PIN, LOW);
}

bool mount() {
  if (mounted) return true;
  if (!cardPresent()) {
    setStored(SD_NONE);
    return false;
  }
  gpio_hold_dis((gpio_num_t)SD_EN_PIN);  // held low through the last deep sleep
  pinMode(SD_EN_PIN, OUTPUT);
  digitalWrite(SD_EN_PIN, HIGH);
  delay(20);  // supply ramp before the first clocks

  // The panel sleeps (or was never woken this boot); keep it deselected while
  // the bus carries card traffic.
  pinMode(EPD_CS_PIN, OUTPUT);
  digitalWrite(EPD_CS_PIN, HIGH);
  SPIClass& spi = display::spi();
  spi.end();
  spi.begin(EPD_SCK_PIN, SD_MISO_PIN, EPD_MOSI_PIN, -1);
  if (!SD.begin(SD_CS_PIN, spi, kSdHz, "/sd", 2)) {
    Serial.println("[sd] mount failed");
    SD.end();
    spi.end();
    powerOff();
    setStored(SD_ERROR);
    return false;
  }
  if (!SD.exists(kDir)) SD.mkdir(kDir);
  mounted = true;
  return true;
}

void unmount() {
  if (!mounted) return;
  SD.end();
  display::spi().end();
  powerOff();
  mounted = false;
}

fs::FS& fs() { return SD; }

// "/log/<strftime(fmt)>" in local time; "/log/no-clock.<ext>" without a clock.
static void datedPath(char* out, size_t len, const char* fmt, const char* noClockName) {
  time_t now = time(nullptr);
  if (now < 1600000000) {
    snprintf(out, len, "%s/%s", kDir, noClockName);
    return;
  }
  struct tm local;
  localtime_r(&now, &local);
  char name[32];
  strftime(name, sizeof(name), fmt, &local);
  snprintf(out, len, "%s/%s", kDir, name);
}

// Oldest day log first (names sort by date), monthly CSVs only when no day
// log is left.
static bool removeOldest() {
  File dir = SD.open(kDir);
  if (!dir) return false;
  String oldestLog, oldestCsv;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    f.close();
    String& slot = name.endsWith(".log") ? oldestLog : oldestCsv;
    if (!slot.length() || name < slot) slot = name;
  }
  dir.close();
  String victim = oldestLog.length() ? oldestLog : oldestCsv;
  if (!victim.length()) return false;
  Serial.printf("[sd] card full, deleting %s\n", victim.c_str());
  return SD.remove(String(kDir) + "/" + victim);
}

static void prune() {
  uint64_t total = SD.totalBytes();
  if (!total) return;
  for (int i = 0; i < 50 && SD.usedBytes() * 100 > total * kPrunePercent; i++) {
    if (!removeOldest()) break;
  }
}

static bool writeLines() {
  if (!used && !dropped) return true;
  char path[40];
  datedPath(path, sizeof(path), "%Y-%m-%d.log", "no-clock.log");
  if (!SD.exists(path)) prune();  // first write of the day
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;
  bool ok = true;  // FS files don't set Print's write error: count the bytes
  size_t pos = 0;
  while (pos + sizeof(RecHeader) <= used) {
    RecHeader h;
    memcpy(&h, buf + pos, sizeof(h));
    pos += sizeof(h);
    char prefix[24];
    if (h.epoch) {
      time_t t = h.epoch;
      struct tm local;
      localtime_r(&t, &local);
      snprintf(prefix, sizeof(prefix), "%02d:%02d:%02d.%03u ", local.tm_hour, local.tm_min,
               local.tm_sec, h.millis);
    } else {
      snprintf(prefix, sizeof(prefix), "+%10lums ", (unsigned long)h.uptime);
    }
    ok = ok && f.print(prefix) == strlen(prefix);
    ok = ok && f.write(buf + pos, h.len) == h.len;
    ok = ok && f.write('\n') == 1;
    pos += h.len;
  }
  if (dropped) f.printf("(%lu log lines dropped: buffer full)\n", (unsigned long)dropped);
  f.close();
  return ok;
}

static bool writeRow() {
  if (!row[0]) return true;
  char path[40];
  datedPath(path, sizeof(path), "wakes-%Y-%m.csv", "wakes-no-clock.csv");
  bool isNew = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;
  if (isNew && rowHeader) f.printf("%s,awake_ms\n", rowHeader);
  char awake[16];
  int n = snprintf(awake, sizeof(awake), ",%lu\n", (unsigned long)millis());
  bool ok = f.print(row) == strlen(row) && f.write((const uint8_t*)awake, n) == (size_t)n;
  f.close();
  return ok;
}

static void clear() {
  used = 0;
  dropped = 0;
  row[0] = '\0';
}

State flush() {
  if (!used && !dropped && !row[0]) return state();
  bool wasMounted = mounted;
  if (!mount()) {
    clear();
    return state();
  }
  flushing = true;
  uint32_t t0 = millis();
  bool ok = writeLines() && writeRow();
  flushing = false;
  Serial.printf("[sd] log %s in %lu ms\n", ok ? "written" : "write FAILED",
                (unsigned long)(millis() - t0));
  setStored(ok ? SD_OK : SD_ERROR);
  clear();
  if (!wasMounted) unmount();
  return ok ? SD_OK : SD_ERROR;
}

void prepareSleep() {
  unmount();
  gpio_hold_dis((gpio_num_t)SD_EN_PIN);
  powerOff();
  pinMode(SD_DET_PIN, INPUT);
  // RTC-capable pad: the hold keeps the card switched off through deep sleep
  // (unheld, the enable line would float).
  gpio_hold_en((gpio_num_t)SD_EN_PIN);
}

}  // namespace sd_log
