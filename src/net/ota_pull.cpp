#include "ota_pull.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>

#include "../../include/version.h"
#include "../util/log.h"
#include "https.h"

namespace ota_pull {

// Returns >0 when b is newer than a (numeric triple compare).
static int semverNewer(const char* a, const char* b) {
  int a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
  sscanf(a, "%d.%d.%d", &a1, &a2, &a3);
  sscanf(b, "%d.%d.%d", &b1, &b2, &b3);
  if (b1 != a1) return b1 - a1;
  if (b2 != a2) return b2 - a2;
  return b3 - a3;
}

bool checkAndUpdate(const Config& cfg) {
  if (!cfg.otaManifestUrl.length()) return false;

  JsonDocument doc;
  if (!net::httpGetJson(cfg.otaManifestUrl, doc)) {
    LOGW("ota", "manifest fetch failed");
    return false;
  }
  const char* version = doc["version"];
  const char* url = doc["url"];
  if (!version || !url) {
    LOGW("ota", "manifest missing version/url");
    return false;
  }
  if (semverNewer(APP_VERSION, version) <= 0) {
    LOGI("ota", "up to date (%s, remote %s)", APP_VERSION, version);
    return false;
  }
  LOGI("ota", "updating %s -> %s from %s", APP_VERSION, version, url);

  HTTPClient http;
  http.setTimeout(60000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub release assets redirect
  if (!http.begin(net::tlsClient(), url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    LOGE("ota", "image GET %d", code);
    http.end();
    return false;
  }

  int len = http.getSize();
  if (len <= 0) {
    LOGE("ota", "no content length");
    http.end();
    return false;
  }
  if (!Update.begin(len)) {
    LOGE("ota", "Update.begin failed: %s", Update.errorString());
    http.end();
    return false;
  }

  size_t written = Update.writeStream(http.getStream());
  http.end();
  if (written != (size_t)len || !Update.end(true)) {
    LOGE("ota", "write failed at %u/%d: %s", (unsigned)written, len, Update.errorString());
    Update.abort();
    return false;
  }

  LOGI("ota", "update installed, reboot pending");
  return true;
}

void markImageValid() {
  // No-op unless the bootloader was built with rollback support; safe always.
  esp_ota_mark_app_valid_cancel_rollback();
}

}  // namespace ota_pull
