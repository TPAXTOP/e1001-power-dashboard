// Shared HTTPS helpers. One TLS client, sequential requests only -
// concurrent TLS connections would not fit internal RAM.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NetworkClientSecure.h>

namespace net {

// TLS client with the embedded curated root store (certs/roots.pem) attached.
NetworkClientSecure& tlsClient();

// GET url, parse JSON body into doc (optionally with a deserialization
// filter). `authorization` is sent verbatim as the Authorization header when
// set. Returns true on HTTP 200 + parse success.
bool httpGetJson(const String& url, JsonDocument& doc, const JsonDocument* filter = nullptr,
                 int* httpCodeOut = nullptr, const char* authorization = nullptr);

// POST a JSON body, parse JSON response. Adds Authorization: Bearer when set.
bool httpPostJson(const String& url, const String& body, JsonDocument& doc,
                  const String& bearerToken = "", int* httpCodeOut = nullptr);

// Requests above keep their TLS connection open (HTTP keep-alive), so the
// next request to the same host skips the handshake. Close it before using
// tlsClient() directly (OTA download) and before WiFi goes off.
void closeAll();

// Per-wake request outcome counters, for telling "no internet" from "a
// server had a bad day" (dash::classifyConnectivity).
struct Counters {
  int attempts = 0;
  int responses = 0;        // got any HTTP status line, even an error
  int transportErrors = 0;  // DNS/TCP/TLS failure, no HTTP status
};
void resetCounters();
const Counters& counters();

}  // namespace net
