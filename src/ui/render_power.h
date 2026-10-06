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
  char outageGroup[8] = "";   // configured group, always shown in the header
  char outageError[48] = "";  // why there is no outage data (shown when !hasOutage)

  bool hasBackup = false;
  bool backupStale = false;
  dash::BackupData backup;

  // Onboard SHT4x, offsets already applied. "Out" = outside the configured
  // comfort range -> drawn inverted.
  bool hasIndoor = false;
  float indoorTemp = 0;
  float indoorRh = 0;
  bool indoorTempOut = false;
  bool indoorRhOut = false;

  char nowLocalIso[20] = "";  // Kyiv "YYYY-MM-DDTHH:MM", filters past forecast hours

  bool widgetWeather = true;
  bool widgetOutage = true;
  bool widgetBackup = true;
};

namespace render_power {

// Compose the 800x450 page area into the display buffer (no refresh); the
// status bar below it is drawn by render_statusbar.
void render(const PowerView& view);

}  // namespace render_power
