#pragma once

#include <Arduino.h>

// Serial (millis prefix, as before) plus the SD card log (wall-clock prefix),
// see store/sd_log.h. Keep secrets out of log lines: they end up on the card.
void logLine(char level, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define LOGI(tag, fmt, ...) logLine('I', tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) logLine('W', tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) logLine('E', tag, fmt, ##__VA_ARGS__)
