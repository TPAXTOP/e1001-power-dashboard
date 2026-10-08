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

static Counters stats;
// True while the open connection on tlsClient() was left by http() below.
// Anything else (the OTA download's own HTTPClient) may have left it pointing
// at another host, which http() would then reuse without noticing.
static bool ownsConnection = false;

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

// One HTTPClient for all requests: with the same object, a request to the
// host of the previous one reuses its open TLS connection (keep-alive), and
// a different host makes HTTPClient close the old connection first. A fresh
// HTTPClient per request would always disconnect, i.e. one TLS handshake per
// request (~1 s of radio time each).
static HTTPClient& http() {
  static HTTPClient client;
  static bool inited = false;
  if (!inited) {
    client.collectAllHeaders();  // logged for error responses only, filtered
    inited = true;
  }
  return client;
}

// "host/path" without scheme and query: the Deye token URL carries the appId.
static String logUrl(const String& url) {
  int start = url.indexOf("://");
  start = start < 0 ? 0 : start + 3;
  int end = url.indexOf('?', start);
  return url.substring(start, end < 0 ? url.length() : end);
}

// Headers that tell a rate limit or an outage apart from a real rejection.
static bool interestingHeader(const String& name) {
  String n = name;
  n.toLowerCase();
  return n == "date" || n == "retry-after" || n == "www-authenticate" || n == "server" ||
         n == "content-type" || n == "cf-ray" || n == "via" || n.indexOf("ratelimit") >= 0 ||
         n.indexOf("rate-limit") >= 0 || n.startsWith("x-");
}

// One line per request; an HTTP error also gets its headers and body start
// (the evidence for "rate limited" vs "key rejected" vs "server broken").
static void logResponse(const char* method, const String& url, int code, uint32_t ms,
                        HTTPClient& h) {
  if (code == HTTP_CODE_OK) {
    LOGI("https", "%s 200 %s %lu ms", method, logUrl(url).c_str(), (unsigned long)ms);
    return;
  }
  if (code <= 0) {
    LOGE("https", "%s %s failed: %s (%d) after %lu ms", method, logUrl(url).c_str(),
         HTTPClient::errorToString(code).c_str(), code, (unsigned long)ms);
    return;
  }
  LOGE("https", "%s %d %s %lu ms", method, code, logUrl(url).c_str(), (unsigned long)ms);
  String headers;
  for (int i = 0; i < h.headers(); i++) {
    if (!interestingHeader(h.headerName(i))) continue;
    headers += h.headerName(i) + ": " + h.header(i) + "; ";
  }
  if (headers.length()) LOGE("https", "  headers: %s", headers.c_str());
  String body = h.getString();
  body.replace("\r", " ");
  body.replace("\n", " ");
  if (body.length() > 240) body = body.substring(0, 240) + "...";
  LOGE("https", "  body (%u B): %s", (unsigned)h.getSize(), body.c_str());
}

void closeAll() {
  http().end();
  tlsClient().stop();
  ownsConnection = false;
}

void resetCounters() { stats = Counters(); }

const Counters& counters() { return stats; }

// Body is read via getString(): HTTPClient then handles chunked
// transfer-encoding for us; large strings land in PSRAM via malloc.
static bool parseResponse(HTTPClient& h, JsonDocument& doc, const JsonDocument* filter) {
  String body = h.getString();
  DeserializationError err =
      filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
             : deserializeJson(doc, body);
  if (err) {
    LOGE("https", "json parse failed: %s (body %u bytes)", err.c_str(), body.length());
    return false;
  }
  return true;
}

// One request with a single retry when a kept-alive connection turned out to
// be dead (the server may close idle connections at any time).
template <typename Send>
static int request(const char* method, const String& url, Send send) {
  HTTPClient& h = http();
  if (!ownsConnection) tlsClient().stop();
  int code = 0;
  for (int tryNo = 0; tryNo < 2; tryNo++) {
    bool reused = tlsClient().connected();
    if (!h.begin(tlsClient(), url)) {
      LOGE("https", "begin failed: %s", logUrl(url).c_str());
      return HTTPC_ERROR_CONNECTION_REFUSED;
    }
    h.setUserAgent(kUserAgent);
    h.setTimeout(20000);
    h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    h.setReuse(true);
    code = send(h);
    if (code > 0 || !reused) break;
    LOGW("https", "%s on a kept-alive connection failed (%d), reconnecting", method, code);
    h.end();
    tlsClient().stop();
  }
  stats.attempts++;
  if (code > 0) {
    stats.responses++;
  } else {
    stats.transportErrors++;
  }
  return code;
}

bool httpGetJson(const String& url, JsonDocument& doc, const JsonDocument* filter,
                 int* httpCodeOut, const char* authorization) {
  HTTPClient& h = http();
  uint32_t t0 = millis();
  int code = request("GET", url, [&](HTTPClient& c) {
    c.addHeader("Accept", "application/json");
    if (authorization && *authorization) c.addHeader("Authorization", authorization);
    return c.GET();
  });
  if (httpCodeOut) *httpCodeOut = code;
  logResponse("GET", url, code, millis() - t0, h);
  bool ok = false;
  if (code == HTTP_CODE_OK) ok = parseResponse(h, doc, filter);
  h.end();  // keeps the connection open when the server allows it
  ownsConnection = tlsClient().connected();
  return ok;
}

bool httpPostJson(const String& url, const String& body, JsonDocument& doc,
                  const String& bearerToken, int* httpCodeOut) {
  HTTPClient& h = http();
  uint32_t t0 = millis();
  int code = request("POST", url, [&](HTTPClient& c) {
    c.addHeader("Content-Type", "application/json");
    c.addHeader("Accept", "application/json");
    if (bearerToken.length()) c.addHeader("Authorization", "Bearer " + bearerToken);
    return c.POST(body);
  });
  if (httpCodeOut) *httpCodeOut = code;
  logResponse("POST", url, code, millis() - t0, h);
  bool ok = false;
  if (code == HTTP_CODE_OK) ok = parseResponse(h, doc, nullptr);
  h.end();
  ownsConnection = tlsClient().connected();
  return ok;
}

}  // namespace net
