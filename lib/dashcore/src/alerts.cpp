#include "alerts.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace dash {

static bool equalsIgnoreCase(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  }
  return *a == *b;
}

AlertType alertTypeFromString(const char* s) {
  if (!s) return ALERT_OTHER;
  if (equalsIgnoreCase(s, "AIR")) return ALERT_AIR;
  if (equalsIgnoreCase(s, "ARTILLERY")) return ALERT_ARTILLERY;
  if (equalsIgnoreCase(s, "URBAN_FIGHTS")) return ALERT_URBAN_FIGHTS;
  if (equalsIgnoreCase(s, "CHEMICAL")) return ALERT_CHEMICAL;
  if (equalsIgnoreCase(s, "NUCLEAR")) return ALERT_NUCLEAR;
  return ALERT_OTHER;
}

AlertLevel alertLevelFromString(const char* s) {
  if (s && equalsIgnoreCase(s, "Yellow")) return ALERT_YELLOW;
  return ALERT_RED;
}

void copyUtf8(char* dst, const char* src, int size) {
  dst[0] = '\0';
  if (!src || size < 1) return;
  while (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r') src++;
  int len = (int)strlen(src);
  if (len > size - 1) {
    len = size - 1;
    // Back up over continuation bytes (10xxxxxx) to the start of the
    // character that did not fit, and cut there.
    while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
  }
  while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\n' ||
                     src[len - 1] == '\r')) {
    len--;
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's algorithm).
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

bool parseIsoUtc(const char* s, uint32_t& epoch) {
  if (!s) return false;
  int y, mo, d, h, mi, sec;
  char sep;
  if (sscanf(s, "%4d-%2d-%2d%c%2d:%2d:%2d", &y, &mo, &d, &sep, &h, &mi, &sec) != 7) return false;
  if ((sep != 'T' && sep != ' ') || y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 ||
      mi > 59 || sec > 60) {
    return false;
  }
  int64_t t = daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + sec;
  if (t < 0 || t > 0xFFFFFFFFLL) return false;
  epoch = (uint32_t)t;
  return true;
}

// Higher = shown first.
static int rank(const AlertEntry& e) { return e.level * 2 + (e.type == ALERT_AIR ? 1 : 0); }

AlertStatus pickAlert(const AlertEntry* entries, int n) {
  AlertStatus out;
  memset(&out, 0, sizeof(out));
  out.level = ALERT_NONE;
  const AlertEntry* best = nullptr;
  for (int i = 0; i < n; i++) {
    const AlertEntry& e = entries[i];
    if (e.level == ALERT_NONE) continue;
    if (!best || rank(e) > rank(*best) ||
        (rank(e) == rank(*best) && e.createdEpoch && (!best->createdEpoch ||
                                                       e.createdEpoch < best->createdEpoch))) {
      best = &e;
    }
  }
  if (!best) return out;
  out.level = best->level;
  out.type = best->type;
  out.sinceEpoch = best->createdEpoch;
  copyUtf8(out.reason, best->reason, sizeof(out.reason));

  // Distinct other types (the same type from a second region is no news).
  uint8_t seen = (uint8_t)(1u << best->type);
  for (int i = 0; i < n; i++) {
    if (entries[i].level == ALERT_NONE) continue;
    uint8_t bit = (uint8_t)(1u << entries[i].type);
    if (!(seen & bit)) {
      seen |= bit;
      out.extraTypes++;
    }
  }
  return out;
}

// Used when the API sends no reason (Red alerts usually come with "").
// Names as in the official alert apps; capitals for Red.
static const char* alertName(const AlertStatus& a) {
  bool red = a.level == ALERT_RED;
  switch (a.type) {
    case ALERT_AIR:
      return red ? "ПОВІТРЯНА ТРИВОГА" : "Повітряна загроза (жовтий рівень)";
    case ALERT_ARTILLERY:
      return red ? "ЗАГРОЗА АРТОБСТРІЛУ" : "Загроза артобстрілу";
    case ALERT_URBAN_FIGHTS:
      return red ? "ЗАГРОЗА ВУЛИЧНИХ БОЇВ" : "Загроза вуличних боїв";
    case ALERT_CHEMICAL:
      return red ? "ХІМІЧНА ЗАГРОЗА" : "Хімічна загроза";
    case ALERT_NUCLEAR:
      return red ? "РАДІАЦІЙНА ЗАГРОЗА" : "Радіаційна загроза";
    default:
      return red ? "ТРИВОГА" : "Увага";
  }
}

void formatAlert(const AlertStatus& a, const char* sinceText, char* buf, int bufLen) {
  if (bufLen < 1) return;
  buf[0] = '\0';
  if (a.level == ALERT_NONE) return;
  const char* text = a.reason[0] ? a.reason : alertName(a);
  int n = snprintf(buf, bufLen, "%s", text);
  if (sinceText && sinceText[0] && n < bufLen) {
    n += snprintf(buf + n, bufLen - n, " з %s", sinceText);
  }
  if (a.extraTypes && n < bufLen) snprintf(buf + n, bufLen - n, " +%u", a.extraTypes);
}

}  // namespace dash
