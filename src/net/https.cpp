#include "https.h"

#include <HTTPClient.h>

#include "../util/log.h"

// Embedded by board_build.embed_txtfiles (null-terminated).
extern const char kRootsPem[] asm("_binary_certs_roots_pem_start");

namespace net {

// Browser-like UA: Yasno sits behind a CDN that may reject obvious bots.
static const char* kUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0 Safari/537.36";

NetworkClientSecure& tlsClient() {
  static NetworkClientSecure client;
  static bool inited = false;
  if (!inited) {
    client.setCACert(kRootsPem);
    client.setHandshakeTimeout(15);
    inited = true;
  }
  return client;
}

// Body is read via getString(): HTTPClient then handles chunked
// transfer-encoding for us; large strings land in PSRAM via malloc.
static bool parseResponse(HTTPClient& http, JsonDocument& doc, const JsonDocument* filter) {
  String body = http.getString();
  DeserializationError err =
      filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
             : deserializeJson(doc, body);
  if (err) {
    LOGE("https", "json parse failed: %s (body %u bytes)", err.c_str(), body.length());
    return false;
  }
  return true;
}

bool httpGetJson(const String& url, JsonDocument& doc, const JsonDocument* filter,
                 int* httpCodeOut) {
  HTTPClient http;
  http.setUserAgent(kUserAgent);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(tlsClient(), url)) {
    LOGE("https", "begin failed: %s", url.c_str());
    return false;
  }
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (httpCodeOut) *httpCodeOut = code;
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    ok = parseResponse(http, doc, filter);
  } else {
    LOGE("https", "GET %d %s", code, url.c_str());
  }
  http.end();
  return ok;
}

bool httpPostJson(const String& url, const String& body, JsonDocument& doc,
                  const String& bearerToken, int* httpCodeOut) {
  HTTPClient http;
  http.setUserAgent(kUserAgent);
  http.setTimeout(20000);
  if (!http.begin(tlsClient(), url)) {
    LOGE("https", "begin failed: %s", url.c_str());
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (bearerToken.length()) {
    http.addHeader("Authorization", "Bearer " + bearerToken);
  }
  int code = http.POST(body);
  if (httpCodeOut) *httpCodeOut = code;
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    ok = parseResponse(http, doc, nullptr);
  } else {
    LOGE("https", "POST %d %s", code, url.c_str());
  }
  http.end();
  return ok;
}

}  // namespace net
