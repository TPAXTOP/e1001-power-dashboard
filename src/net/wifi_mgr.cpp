#include "wifi_mgr.h"

#include <WiFi.h>

#include "../util/log.h"

namespace wifi_mgr {

bool connect(const Config& cfg) {
  if (!cfg.hasWifi()) return false;

  WiFi.persistent(false);  // creds live in our own NVS namespace
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < cfg.wifiTimeoutMs) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOGW("wifi", "connect timeout (%lu ms), ssid=%s", cfg.wifiTimeoutMs, cfg.wifiSsid.c_str());
    return false;
  }
  LOGI("wifi", "connected, ip=%s rssi=%d (%lu ms)", WiFi.localIP().toString().c_str(),
       WiFi.RSSI(), millis() - start);
  return true;
}

void disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace wifi_mgr
