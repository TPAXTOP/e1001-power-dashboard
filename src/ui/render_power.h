#pragma once

#include <data_model.h>

// Everything the power screen needs, assembled by the wake cycle.
// "has" = data (fresh or cached) exists; "stale" = this wake's refresh failed
// and the cached copy is shown (renders the "!" badge, like the web app).
struct PowerView {
  bool hasWeather = false;
  bool weatherStale = false;
  dash::WeatherData weather;

  bool hasOutage = false;
  bool outageStale = false;
  dash::OutageSchedule outage;

  bool hasBackup = false;
  bool backupStale = false;
  dash::BackupData backup;

  char nowLocalIso[20] = "";  // Kyiv "YYYY-MM-DDTHH:MM", filters past forecast hours

  bool widgetWeather = true;
  bool widgetOutage = true;
  bool widgetBackup = true;
};

namespace render_power {

// Compose the 800x480 screen into the display buffer (no refresh).
void render(const PowerView& view);

}  // namespace render_power
