#include "weather_api.h"

#include <ArduinoJson.h>

#include "../net/https.h"
#include "../util/log.h"

namespace weather_api {

bool fetch(const Config& cfg, dash::WeatherData& out) {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + cfg.weatherLat +
               "&longitude=" + cfg.weatherLon +
               "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code" +
               "&hourly=temperature_2m,weather_code,precipitation_probability" +
               "&forecast_hours=8&timezone=Europe%2FKyiv";

  JsonDocument doc;
  if (!net::httpGetJson(url, doc)) return false;

  JsonObject current = doc["current"];
  if (current.isNull()) {
    LOGE("weather", "payload missing current");
    return false;
  }

  memset(&out, 0, sizeof(out));
  out.temperature = current["temperature_2m"] | 0.0f;
  out.humidity = current["relative_humidity_2m"] | 0;
  out.windSpeed = current["wind_speed_10m"] | 0.0f;
  out.weatherCode = current["weather_code"] | 0;
  strlcpy(out.time, current["time"] | "", sizeof(out.time));

  JsonArray times = doc["hourly"]["time"];
  JsonArray temps = doc["hourly"]["temperature_2m"];
  JsonArray codes = doc["hourly"]["weather_code"];
  JsonArray probs = doc["hourly"]["precipitation_probability"];  // may be absent or hold nulls
  int n = 0;
  for (size_t i = 0; i < times.size() && n < dash::kHourlyMax; i++) {
    if (i >= temps.size() || i >= codes.size()) break;
    dash::HourlyForecast& h = out.hourly[n];
    strlcpy(h.time, times[i] | "", sizeof(h.time));
    h.temperature = temps[i] | 0.0f;
    h.weatherCode = codes[i] | 0;
    h.precipProb = (i < probs.size() && probs[i].is<int>())
                       ? (uint8_t)constrain(probs[i].as<int>(), 0, 100)
                       : dash::kPrecipUnknown;
    n++;
  }
  out.hourlyCount = n;

  LOGI("weather", "ok: %.1fC code=%d hourly=%d", out.temperature, out.weatherCode, n);
  return true;
}

}  // namespace weather_api
