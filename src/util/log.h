#pragma once

#include <Arduino.h>

#define LOGI(tag, fmt, ...) Serial.printf("[%8lu] I %-8s " fmt "\n", millis(), tag, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) Serial.printf("[%8lu] W %-8s " fmt "\n", millis(), tag, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) Serial.printf("[%8lu] E %-8s " fmt "\n", millis(), tag, ##__VA_ARGS__)
