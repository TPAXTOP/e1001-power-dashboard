#include "ota_pull.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <semver.h>

#include "../../include/version.h"
#include "../util/log.h"
#include "https.h"

// Embedded by board_build.embed_txtfiles (null-terminated).
extern const char kOtaPubPem[] asm("_binary_certs_ota_signing_pub_pem_start");

namespace ota_pull {

static void toHex(const uint8_t* in, size_t len, char* out) {
  for (size_t i = 0; i < len; i++) sprintf(out + i * 2, "%02x", in[i]);
}

// The release workflow signs "<version>\n<sha256hex>\n" with ECDSA P-256.
// Binding the version stops an old signed image being replayed as "newer".
static bool signatureValid(const char* version, const char* shaHex, const char* sigB64) {
  uint8_t sig[128];
  size_t sigLen = 0;
  if (mbedtls_base64_decode(sig, sizeof(sig), &sigLen, (const uint8_t*)sigB64,
                            strlen(sigB64)) != 0) {
    LOGE("ota", "signature is not valid base64");
    return false;
  }

  String msg = String(version) + "\n" + shaHex + "\n";
  uint8_t digest[32];
  mbedtls_sha256((const uint8_t*)msg.c_str(), msg.length(), digest, 0);

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, (const uint8_t*)kOtaPubPem, strlen(kOtaPubPem) + 1);
  if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), sig, sigLen);
  mbedtls_pk_free(&pk);
  if (rc != 0) LOGE("ota", "signature check failed (-0x%04x)", -rc);
  return rc == 0;
}

// Streams `len` bytes into the inactive slot, hashing as it goes.
static bool download(NetworkClient& stream, size_t len, uint8_t sha[32]) {
  const size_t kBuf = 4096;
  uint8_t* buf = static_cast<uint8_t*>(malloc(kBuf));
  if (!buf) return false;

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);

  size_t total = 0;
  uint32_t lastData = millis();
  bool ok = true;
  while (total < len) {
    size_t avail = stream.available();
    if (!avail) {
      if (millis() - lastData > 30000) {
        LOGE("ota", "download stalled at %u/%u", (unsigned)total, (unsigned)len);
        ok = false;
        break;
      }
      delay(5);
      continue;
    }
    size_t want = avail < kBuf ? avail : kBuf;
    if (want > len - total) want = len - total;
    int n = stream.read(buf, want);
    if (n <= 0) continue;
    mbedtls_sha256_update(&ctx, buf, n);
    if (Update.write(buf, n) != (size_t)n) {
      LOGE("ota", "flash write failed: %s", Update.errorString());
      ok = false;
      break;
    }
    total += n;
    lastData = millis();
  }

  mbedtls_sha256_finish(&ctx, sha);
  mbedtls_sha256_free(&ctx);
  free(buf);
  return ok;
}

Result checkAndUpdate(const Config& cfg, const String& skipVersion) {
  Result r;
  if (!cfg.otaManifestUrl.length()) {
    r.message = "no manifest URL configured";
    return r;
  }

  JsonDocument doc;
  if (!net::httpGetJson(cfg.otaManifestUrl, doc)) {
    r.message = "manifest fetch failed";
    LOGW("ota", "%s", r.message.c_str());
    return r;
  }
  const char* version = doc["version"];
  const char* url = doc["url"];
  const char* shaHex = doc["sha256"];
  const char* sig = doc["sig"];
  size_t size = doc["size"] | 0;
  if (!version || !url || !shaHex || !sig || !size || strlen(shaHex) != 64 ||
      !dash::semverValid(version)) {
    r.message = "manifest incomplete or unsigned, ignored";
    LOGW("ota", "%s", r.message.c_str());
    return r;
  }
  r.version = version;

  if (dash::semverCompare(version, APP_VERSION) <= 0) {
    r.message = String("up to date (") + APP_VERSION + ", latest " + version + ")";
    LOGI("ota", "%s", r.message.c_str());
    return r;
  }
  if (skipVersion == version) {
    r.message = String(version) + " was rolled back earlier, skipped";
    LOGW("ota", "%s", r.message.c_str());
    return r;
  }
  // Cheap check first: no point downloading an image the key never signed.
  if (!signatureValid(version, shaHex, sig)) {
    r.message = "manifest signature invalid, update refused";
    return r;
  }

  LOGI("ota", "updating %s -> %s (%u bytes) from %s", APP_VERSION, version, (unsigned)size, url);
  HTTPClient http;
  http.setTimeout(60000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub release assets redirect
  if (!http.begin(net::tlsClient(), url)) {
    r.message = "image request failed";
    return r;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    LOGE("ota", "image GET %d", code);
    r.message = String("image download failed (HTTP ") + code + ")";
    http.end();
    return r;
  }
  int len = http.getSize();
  if (len <= 0 || (size_t)len != size) {
    LOGE("ota", "image is %d bytes, manifest says %u", len, (unsigned)size);
    r.message = "image size does not match the manifest";
    http.end();
    return r;
  }
  if (!Update.begin(size)) {
    LOGE("ota", "Update.begin failed: %s", Update.errorString());
    r.message = String("cannot start update: ") + Update.errorString();
    http.end();
    return r;
  }

  uint8_t sha[32];
  bool ok = download(http.getStream(), size, sha);
  http.end();

  char gotHex[65];
  toHex(sha, sizeof(sha), gotHex);
  if (!ok) {
    r.message = "download failed";
  } else if (strcasecmp(gotHex, shaHex) != 0) {
    LOGE("ota", "sha256 mismatch: got %s", gotHex);
    r.message = "image hash does not match the manifest";
    ok = false;
  } else if (!Update.end()) {
    LOGE("ota", "Update.end failed: %s", Update.errorString());
    r.message = String("image rejected: ") + Update.errorString();
    ok = false;
  }
  if (!ok) {
    Update.abort();  // the boot partition stays unchanged
    return r;
  }

  r.installed = true;
  r.message = String("installed ") + version + ", rebooting";
  LOGI("ota", "%s", r.message.c_str());
  return r;
}

void noteBootedVersion(PersistedState& st) {
  if (!st.otaTriedVersion.length()) return;
  if (st.otaTriedVersion != APP_VERSION) {
    LOGW("ota", "%s was rolled back, back on %s", st.otaTriedVersion.c_str(), APP_VERSION);
    st.otaBadVersion = st.otaTriedVersion;
  }
  st.otaTriedVersion = "";
}

bool pendingVerify() {
  esp_ota_img_states_t state;
  return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

void confirmIfPending() {
  if (!pendingVerify()) return;
  esp_ota_mark_app_valid_cancel_rollback();
  LOGI("ota", "image %s confirmed", APP_VERSION);
}

}  // namespace ota_pull
