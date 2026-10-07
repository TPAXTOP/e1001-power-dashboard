#include "wifi_mgr.h"

#include <WiFi.h>

#include "../store/rtc_state.h"
#include "../util/log.h"

namespace wifi_mgr {

// With the AP's BSSID and channel from the last join (RTC RAM), the station
// skips the channel scan: ~0.5 s to associate instead of 2-3 s.
static const uint32_t kFastJoinMs = 4000;

static bool waitConnected(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(50);
  return WiFi.status() == WL_CONNECTED;
}

bool connect(const Config& cfg) {
  if (!cfg.hasWifi()) return false;

  WiFi.persistent(false);  // creds live in our own NVS namespace
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);

  RtcState& rs = rtc_state::get();
  uint32_t start = millis();
  bool ok = false;
  if (rs.hasBssid && rs.channel) {
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str(), rs.channel, rs.bssid, true);
    ok = waitConnected(kFastJoinMs < cfg.wifiTimeoutMs ? kFastJoinMs : cfg.wifiTimeoutMs);
    if (!ok) {
      LOGW("wifi", "fast join (ch %u) failed, scanning", rs.channel);
      rs.hasBssid = false;
      WiFi.disconnect();
    }
  }
  if (!ok) {
    uint32_t spent = millis() - start;
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    ok = waitConnected(cfg.wifiTimeoutMs > spent ? cfg.wifiTimeoutMs - spent : 1000);
  }

  if (!ok) {
    LOGW("wifi", "connect timeout (%lu ms), ssid=%s", millis() - start, cfg.wifiSsid.c_str());
    return false;
  }
  const uint8_t* bssid = WiFi.BSSID();
  if (bssid) {
    memcpy(rs.bssid, bssid, sizeof(rs.bssid));
    rs.channel = (uint8_t)WiFi.channel();
    rs.hasBssid = rs.channel != 0;
  }
  LOGI("wifi", "connected, ip=%s rssi=%d ch=%d (%lu ms)", WiFi.localIP().toString().c_str(),
       WiFi.RSSI(), (int)WiFi.channel(), millis() - start);
  return true;
}

void disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace wifi_mgr
