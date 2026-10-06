#include "fx_api.h"

#include <ArduinoJson.h>
#include <time.h>

#include <algorithm>

#include "../net/https.h"
#include "../net/time_sync.h"
#include "../util/log.h"

namespace fx_api {

static void epochToIsoDate(uint32_t epoch, char* buf, int bufLen) {
  // Kyiv calendar date (TZ is set by time_sync), like every other date shown.
  time_t t = epoch;
  struct tm local;
  localtime_r(&t, &local);
  snprintf(buf, bufLen, "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1,
           local.tm_mday);
}

bool fetch(const Config& cfg, dash::FxData& out) {
  if (!cfg.fxApiKey.length()) {
    LOGW("fx", "no API key configured");
    return false;
  }

  uint32_t now = time_sync::nowEpoch();
  char startDate[11], endDate[11];
  epochToIsoDate(now - (uint32_t)cfg.fxHistoryDays * 86400, startDate, sizeof(startDate));
  epochToIsoDate(now, endDate, sizeof(endDate));

  String url = "https://api.exchangerate.host/timeframe?base=" + cfg.fxBase +
               "&symbols=" + cfg.fxTarget + "&start_date=" + startDate +
               "&end_date=" + endDate + "&source=" + cfg.fxBase +
               "&places=4&amount=1&access_key=" + cfg.fxApiKey;

  JsonDocument doc;
  if (!net::httpGetJson(url, doc)) return false;

  if (doc["success"].is<bool>() && !doc["success"].as<bool>()) {
    LOGE("fx", "upstream error: %s", doc["error"]["info"] | "?");
    return false;
  }

  memset(&out, 0, sizeof(out));
  String targetPair = cfg.fxBase + cfg.fxTarget;
  int n = 0;

  // Format A: rates: { "YYYY-MM-DD": { "UAH": 41.2 } }
  JsonObjectConst rates = doc["rates"];
  if (!rates.isNull()) {
    for (JsonPairConst kv : rates) {
      if (n >= dash::kFxMax) break;
      JsonVariantConst v = kv.value()[cfg.fxTarget];
      if (!v.is<float>()) continue;
      strlcpy(out.points[n].date, kv.key().c_str(), sizeof(out.points[n].date));
      out.points[n].value = v.as<float>();
      n++;
    }
  }

  // Format B: quotes: { "YYYY-MM-DD": { "USDUAH": 41.2 } } or { "USDUAH": 41.2 }
  if (n == 0) {
    JsonObjectConst quotes = doc["quotes"];
    for (JsonPairConst kv : quotes) {
      if (n >= dash::kFxMax) break;
      JsonVariantConst outer = kv.value();
      if (outer.is<float>()) {
        if (targetPair == kv.key().c_str()) {
          uint32_t ts = doc["timestamp"] | now;
          epochToIsoDate(ts, out.points[n].date, sizeof(out.points[n].date));
          out.points[n].value = outer.as<float>();
          n++;
        }
        continue;
      }
      JsonVariantConst v = outer[targetPair];
      if (!v.is<float>()) {
        // fallback: any key ending with the target currency
        for (JsonPairConst inner : outer.as<JsonObjectConst>()) {
          String k(inner.key().c_str());
          if (k.endsWith(cfg.fxTarget) && inner.value().is<float>()) {
            v = inner.value();
            break;
          }
        }
      }
      if (v.is<float>()) {
        strlcpy(out.points[n].date, kv.key().c_str(), sizeof(out.points[n].date));
        out.points[n].value = v.as<float>();
        n++;
      }
    }
  }

  if (n == 0) {
    LOGE("fx", "no rates/quotes in payload");
    return false;
  }

  std::sort(out.points, out.points + n, [](const dash::FxPoint& a, const dash::FxPoint& b) {
    return strcmp(a.date, b.date) < 0;
  });

  out.count = n;
  out.latest = out.points[n - 1];
  out.minValue = out.points[0].value;
  out.maxValue = out.points[0].value;
  for (int i = 1; i < n; i++) {
    out.minValue = std::min(out.minValue, out.points[i].value);
    out.maxValue = std::max(out.maxValue, out.points[i].value);
  }
  out.updatedAtEpoch = doc["timestamp"] | now;

  LOGI("fx", "ok: %d points, latest %s=%.4f", n, out.latest.date, out.latest.value);
  return true;
}

}  // namespace fx_api
