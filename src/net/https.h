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
// filter). Returns true on HTTP 200 + parse success.
bool httpGetJson(const String& url, JsonDocument& doc, const JsonDocument* filter = nullptr,
                 int* httpCodeOut = nullptr);

// POST a JSON body, parse JSON response. Adds Authorization: Bearer when set.
bool httpPostJson(const String& url, const String& body, JsonDocument& doc,
                  const String& bearerToken = "", int* httpCodeOut = nullptr);

}  // namespace net
