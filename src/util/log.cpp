#include "log.h"

#include <stdarg.h>

#include "../store/sd_log.h"

void logLine(char level, const char* tag, const char* fmt, ...) {
  char msg[320];  // longer lines (URLs) are cut
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  Serial.printf("[%8lu] %c %-8s %s\n", millis(), level, tag, msg);
  sd_log::append(level, tag, msg);
}
