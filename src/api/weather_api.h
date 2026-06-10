#pragma once

#include <data_model.h>

#include "../store/config_store.h"

namespace weather_api {

// Open-Meteo current + 8h hourly forecast for the configured coordinates.
bool fetch(const Config& cfg, dash::WeatherData& out);

}  // namespace weather_api
