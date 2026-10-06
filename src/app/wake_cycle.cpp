#include "wake_cycle.h"

#include <Arduino.h>
#include <string.h>
#include <time.h>

#include "../../include/defaults.h"
#include "../api/deye_api.h"
#include "../api/fx_api.h"
#include "../api/weather_api.h"
#include "../api/yasno_api.h"
#include "../net/ota_pull.h"
#include "../net/time_sync.h"
#include "../net/wifi_mgr.h"
#include "../ui/display.h"
#include "../ui/render_fx.h"
#include "../ui/render_power.h"
#include "../ui/render_system.h"
#include "../util/log.h"
#include "power_mgmt.h"

namespace wake_cycle {

// Fetch-or-cache for one source. Calls fetchFn only when online and the last
// success is older than maxAge. Falls back to the NVS cache blob.
// has   <- usable data in `out`
// stale <- a refresh was due but failed; cached (old) data is shown
template <typename T, typename F>
static void acquire(Source src, uint32_t maxAgeS, bool online, PersistedState& st, T& out,
                    bool& has, bool& stale, F fetchFn) {
  uint32_t now = time_sync::nowEpoch();
  // Wakes are aligned to the interval, so with interval == maxAge the age is
  // a few seconds short of maxAge each wake; without slack every other wake
  // would skip the fetch. A last-success in the future (clock was wrong)
  // wraps to a huge age and refetches.
  uint32_t age = now - st.lastSuccessEpoch[src];
  bool fresh = st.lastSuccessEpoch[src] != 0 && age + 60 < maxAgeS;

  bool fetched = false;
  if (online && !fresh) {
    fetched = fetchFn(out);
    if (fetched) {
      state_store::saveBlob(src, &out, sizeof(T));
      st.lastSuccessEpoch[src] = now;
    }
  }

  if (fetched) {
    has = true;
    stale = false;
  } else {
    has = state_store::loadBlob(src, &out, sizeof(T));
    stale = has && !fresh;  // cache exists but a due refresh failed (or offline)
  }
}

// Cached Yasno data crossing midnight: yesterday's "tomorrow" is now "today".
static void rollOverOutageDays(dash::OutageSchedule& sched) {
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  char today[11];
  snprintf(today, sizeof(today), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1,
           local.tm_mday);

  if (sched.today.present && strcmp(sched.today.date, today) != 0) {
    if (sched.tomorrow.present && strcmp(sched.tomorrow.date, today) == 0) {
      sched.today = sched.tomorrow;
      sched.tomorrow.present = false;
      sched.tomorrow.slotCount = 0;
    } else {
      // Cache predates yesterday - both days are useless.
      sched.today.present = false;
      sched.tomorrow.present = false;
    }
  }
}

uint32_t run(Config& cfg, PersistedState& st, bool pageButton, bool coldBoot) {
  st.bootCount++;

  // --- battery policy -------------------------------------------------
  float vbat = power_mgmt::batteryVolts();
  bool usb = power_mgmt::usbPresent();
  LOGI("cycle", "boot=%lu vbat=%.2fV usb=%d", st.bootCount, vbat, usb);

  if (!usb && vbat > 0.5f && vbat < cfg.vbatCrit) {
    display::begin();
    render_system::renderBatteryEmpty(vbat);
    display::show();
    display::hibernate();
    state_store::save(st);
    power_mgmt::deepSleep(0);  // button-only wake
  }
  bool lowBatt = !usb && vbat > 0.5f && vbat < cfg.vbatLow;

  // --- time + network -------------------------------------------------
  time_sync::initFromRtc(cfg);
  bool online = wifi_mgr::connect(cfg);
  if (online) {
    st.consecWifiFails = 0;
    uint32_t now = time_sync::nowEpoch();
    // now < lastSntpEpoch (clock jumped back) wraps to a huge age -> resync.
    if (coldBoot || !time_sync::timeValid() || st.lastSntpEpoch == 0 ||
        now - st.lastSntpEpoch > cfg.sntpIntervalS) {
      if (time_sync::syncSntp(cfg)) st.lastSntpEpoch = time_sync::nowEpoch();
    }
  } else {
    st.consecWifiFails++;
  }

  // Without valid wall time the per-source age logic and all timestamps are
  // meaningless; only fetch when time is sane (first boot needs one SNTP).
  bool timeOk = time_sync::timeValid();

  // --- page selection --------------------------------------------------
  if (pageButton) st.lastPage = (st.lastPage + 1) % 2;

  // --- per-source acquire ----------------------------------------------
  PowerView view;
  view.widgetWeather = cfg.widgetWeather;
  view.widgetOutage = cfg.widgetOutage;
  view.widgetBackup = cfg.widgetBackup;

  bool net = online && timeOk;
  if (cfg.widgetWeather) {
    acquire(SRC_WEATHER, cfg.weatherMaxAgeS, net, st, view.weather, view.hasWeather,
            view.weatherStale,
            [&](dash::WeatherData& out) { return weather_api::fetch(cfg, out); });
  }
  if (cfg.widgetOutage) {
    strlcpy(view.outageGroup, cfg.yasnoGroup.c_str(), sizeof(view.outageGroup));
    yasno_api::FetchError yasnoErr = yasno_api::ERR_NONE;
    bool yasnoTried = false;
    auto fetchOutage = [&](dash::OutageSchedule& out) {
      yasnoTried = true;
      return yasno_api::fetch(cfg, out, &yasnoErr);
    };
    acquire(SRC_OUTAGE, cfg.outageMaxAgeS, net, st, view.outage, view.hasOutage,
            view.outageStale, fetchOutage);
    if (view.hasOutage && strcmp(view.outage.groupId, view.outageGroup) != 0) {
      // Group was changed in the portal: the cache belongs to the old group.
      // Force a fetch now and never show another group's schedule.
      st.lastSuccessEpoch[SRC_OUTAGE] = 0;
      acquire(SRC_OUTAGE, cfg.outageMaxAgeS, net, st, view.outage, view.hasOutage,
              view.outageStale, fetchOutage);
      if (view.hasOutage && strcmp(view.outage.groupId, view.outageGroup) != 0) {
        view.hasOutage = false;
      }
    }
    if (view.hasOutage && timeOk) rollOverOutageDays(view.outage);

    if (!view.hasOutage) {
      if (yasnoTried && yasnoErr == yasno_api::ERR_GROUP_MISSING) {
        snprintf(view.outageError, sizeof(view.outageError),
                 "Group %s not found - check settings", view.outageGroup);
      } else if (!online) {
        strlcpy(view.outageError, "No WiFi connection", sizeof(view.outageError));
      } else if (!timeOk) {
        strlcpy(view.outageError, "Clock not set (NTP failed)", sizeof(view.outageError));
      } else {
        strlcpy(view.outageError, "Yasno request failed", sizeof(view.outageError));
      }
    }
  }
  if (cfg.widgetBackup && cfg.hasDeye()) {
    acquire(SRC_BACKUP, cfg.backupMaxAgeS, net, st, view.backup, view.hasBackup,
            view.backupStale,
            [&](dash::BackupData& out) { return deye_api::fetch(cfg, st, out); });
  }

  dash::FxData fx;
  bool hasFx = false, fxStale = false;
  if (st.lastPage == 1) {
    acquire(SRC_FX, cfg.fxMaxAgeS, net, st, fx, hasFx, fxStale,
            [&](dash::FxData& out) { return fx_api::fetch(cfg, out); });
  }

  // --- render ----------------------------------------------------------
  if (timeOk) {
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    snprintf(view.nowLocalIso, sizeof(view.nowLocalIso), "%04d-%02d-%02dT%02d:%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
             local.tm_min);
  }

  display::begin();
  if (st.lastPage == 1) {
    render_fx::render(hasFx, fxStale, fx);
  } else {
    render_power::render(view);
  }
  display::show();
  display::hibernate();

  // A freshly OTA-updated image that reached this point works; accept it.
  if (st.otaPendingVerify) {
    ota_pull::markImageValid();
    st.otaPendingVerify = false;
    LOGI("cycle", "OTA image verified OK");
  }

  // --- periodic OTA check (skipped on low battery) ----------------------
  if (net && !lowBatt && cfg.otaManifestUrl.length() && cfg.otaEveryN > 0 &&
      st.bootCount % cfg.otaEveryN == 0) {
    if (ota_pull::checkAndUpdate(cfg)) {
      st.otaPendingVerify = true;
      state_store::save(st);
      wifi_mgr::disconnect();
      ESP.restart();
    }
  }

  state_store::save(st);
  wifi_mgr::disconnect();

  // --- sleep duration ----------------------------------------------------
  uint32_t interval = cfg.wakeIntervalS;
  if (lowBatt) interval *= 4;
  if (st.consecWifiFails >= 3) interval *= 2;

  uint32_t sleepS = interval;
  if (timeOk) {
    // Align wakes to interval boundaries so on-screen times look regular.
    uint32_t now = time_sync::nowEpoch();
    sleepS = interval - (now % interval);
    if (sleepS < 30) sleepS += interval;
  }
  return sleepS;
}

}  // namespace wake_cycle
