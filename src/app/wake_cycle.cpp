#include "wake_cycle.h"

#include <Arduino.h>
#include <schedule.h>
#include <status.h>
#include <string.h>
#include <time.h>

#include "../../include/defaults.h"
#include "../api/deye_api.h"
#include "../api/fx_api.h"
#include "../api/weather_api.h"
#include "../api/yasno_api.h"
#include "../hw/sht4x.h"
#include "../net/https.h"
#include "../net/ota_pull.h"
#include "../net/time_sync.h"
#include "../net/wifi_mgr.h"
#include "../store/rtc_state.h"
#include "../ui/display.h"
#include "../ui/render_system.h"
#include "../ui/screen.h"
#include "../util/log.h"
#include "power_mgmt.h"

namespace wake_cycle {

// A task due within this much is run now rather than waking again for it.
static const uint32_t kDueSlackS = 60;
// Once WiFi is up anyway, tasks due within this much ride along (e.g. the
// 10-min outage check on a 3-min inverter wake).
static const uint32_t kPiggybackS = 120;
static const uint32_t kMinSleepS = 60;
static const uint32_t kMaxSleepS = 3600;
static const uint32_t kNoClockSleepS = 600;  // until SNTP works, wake on a plain timer
// Below this the fast partial waveform (fixed temperature) is out of spec.
static const float kColdPanelC = 12.0f;
// Wake a few seconds after an event, so "now" is safely past it.
static const uint32_t kEventLagS = 5;

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

// --- task table ----------------------------------------------------------------

struct Plan {
  uint32_t interval[SRC_COUNT] = {};  // effective; 0 = source off
  uint32_t dueAt[SRC_COUNT] = {};
};

static void planTasks(const Config& cfg, const PersistedState& st, const RtcState& rs,
                      const dash::CadenceRules& rules, bool fxPage, uint32_t now, Plan& plan) {
  uint32_t base[SRC_COUNT] = {};
  base[SRC_WEATHER] = cfg.widgetWeather ? cfg.weatherMaxAgeS : 0;
  base[SRC_OUTAGE] = cfg.widgetOutage ? cfg.outageMaxAgeS : 0;
  bool deye = cfg.widgetBackup && cfg.hasDeye();
  base[SRC_BACKUP] = deye ? cfg.backupMaxAgeS : 0;
  base[SRC_SOC] = deye ? cfg.socMaxAgeS : 0;
  base[SRC_FX] = fxPage ? cfg.fxMaxAgeS : 0;
  for (int i = 0; i < SRC_COUNT; i++) {
    plan.interval[i] = dash::effectiveInterval(base[i], true, rules);
    plan.dueAt[i] =
        dash::taskDueAt(st.lastSuccessEpoch[i], rs.lastAttemptEpoch[i], plan.interval[i], now);
  }
}

// --- status bar ------------------------------------------------------------------

struct StaleInfo {
  bool stale;
  Source src;
  const char* name;
};

// One message by priority (problems > outage countdown > low battery), plus
// the connectivity icon and the device battery. Every duration is quantized:
// the panel is only refreshed when the frame changes.
static void buildStatusBar(const Config& cfg, const PersistedState& st, RtcState& rs,
                           const screen::Frame& fr, bool timeOk, bool lowBatt, float vbat,
                           bool inverterOffline, StatusBarView& sb) {
  const PowerView& view = fr.power;
  uint32_t now = time_sync::nowEpoch();
  char dur[12];
  auto age = [&](uint32_t since) {
    dash::formatDuration(dash::quantizeAge(now - since), dur, sizeof(dur));
    return dur;
  };

  const StaleInfo stale[] = {
      {cfg.widgetOutage && view.outageStale, SRC_OUTAGE, "Outage schedule"},
      {cfg.widgetBackup && view.backupStale && !inverterOffline, SRC_BACKUP, "Inverter"},
      {cfg.widgetWeather && view.weatherStale, SRC_WEATHER, "Weather"},
      {fr.page == screen::PAGE_FX && fr.fxStale, SRC_FX, "Exchange rate"},
  };
  const StaleInfo* firstStale = nullptr;
  for (const StaleInfo& s : stale) {
    if (s.stale && st.lastSuccessEpoch[s.src] && now > st.lastSuccessEpoch[s.src]) {
      firstStale = &s;
      break;
    }
  }

  char outageMsg[sizeof(sb.message)];
  bool hasOutageMsg = false;
  if (timeOk && view.hasOutage) {
    time_t t = now;
    struct tm local;
    localtime_r(&t, &local);
    hasOutageMsg = dash::formatOutageStatus(view.outage, local.tm_hour * 60 + local.tm_min,
                                            outageMsg, sizeof(outageMsg));
  }

  char* msg = sb.message;
  const size_t len = sizeof(sb.message);
  sb.severity = StatusBarView::SEV_WARN;
  sb.connectivity = rs.connectivity;
  if (!timeOk) {
    strlcpy(msg, "Clock not set, waiting for time sync", len);
  } else if (rs.connectivity == dash::CONN_NO_WIFI) {
    if (st.lastOnlineEpoch && now > st.lastOnlineEpoch) {
      snprintf(msg, len, "No WiFi for %s", age(st.lastOnlineEpoch));
    } else {
      strlcpy(msg, "No WiFi", len);
    }
  } else if (rs.connectivity == dash::CONN_NO_INTERNET) {
    if (st.lastInternetEpoch && now > st.lastInternetEpoch) {
      snprintf(msg, len, "No internet for %s", age(st.lastInternetEpoch));
    } else {
      strlcpy(msg, "No internet", len);
    }
  } else if (inverterOffline) {
    snprintf(msg, len, "Inverter offline for %s", age(view.backup.lastUpdateEpoch));
  } else if (firstStale) {
    snprintf(msg, len, "%s data %s old", firstStale->name, age(st.lastSuccessEpoch[firstStale->src]));
  } else if (hasOutageMsg) {
    sb.severity = StatusBarView::SEV_INFO;
    strlcpy(msg, outageMsg, len);
  } else if (lowBatt) {
    strlcpy(msg, "Battery low, refreshing less often", len);
  } else {
    sb.severity = StatusBarView::SEV_NONE;
  }

  int raw = dash::batteryPercentFromVolts(vbat);
  sb.batteryPercent = dash::stickyBatteryStep(raw, rs.shownBattery);
  rs.shownBattery = (int16_t)sb.batteryPercent;
  if (timeOk && st.lastFullEpoch && now >= st.lastFullEpoch) {
    sb.hasSinceFull = true;
    uint32_t since = now - st.lastFullEpoch;
    sb.sinceFullS = dash::quantizeAge(since);
    sb.drainPerDay = dash::drainPerDay(since, sb.batteryPercent);
  }
}

static void outageErrorText(const PersistedState& st, const RtcState& rs,
                            bool timeOk, PowerView& view) {
  size_t n = sizeof(view.outageError);
  if (rs.outageErr == yasno_api::ERR_GROUP_MISSING) {
    snprintf(view.outageError, n, "Group %s not found - check settings", view.outageGroup);
  } else if (rs.connectivity == dash::CONN_NO_WIFI) {
    strlcpy(view.outageError, "No WiFi connection", n);
  } else if (!timeOk) {
    strlcpy(view.outageError, "Clock not set (NTP failed)", n);
  } else if (!st.lastSuccessEpoch[SRC_OUTAGE] && !rs.lastAttemptEpoch[SRC_OUTAGE]) {
    strlcpy(view.outageError, "Not fetched yet", n);
  } else {
    strlcpy(view.outageError, "Yasno request failed", n);
  }
}

// --- the wake ----------------------------------------------------------------------

uint32_t run(Config& cfg, PersistedState& st, const Wake& wake) {
  st.bootCount++;
  RtcState& rs = rtc_state::get();

  // --- battery policy -------------------------------------------------
  float vbat = power_mgmt::batteryVolts();
  bool usb = power_mgmt::usbPresent();
  LOGI("cycle", "boot=%lu vbat=%.2fV usb=%d", st.bootCount, vbat, usb);

  if (!usb && vbat > 0.5f && vbat < cfg.vbatCrit) {
    display::begin();
    render_system::renderBatteryEmpty(vbat);
    display::show();
    screen::invalidate();
    state_store::save(st);
    rtc_state::commit();
    power_mgmt::deepSleep(0);  // button-only wake
  }
  bool lowBatt = !usb && vbat > 0.5f && vbat < cfg.vbatLow;

  // Indoor climate before WiFi and the panel refresh warm up the board.
  float indoorT = 0, indoorRh = 0;
  bool hasIndoor = sht4x::read(indoorT, indoorRh);
  if (hasIndoor) LOGI("cycle", "indoor raw %.1fC %.0f%%", indoorT, indoorRh);

  // --- clock and plan ---------------------------------------------------
  time_sync::initFromRtc(cfg);
  bool timeOk = time_sync::timeValid();
  uint32_t now = time_sync::nowEpoch();

  if (wake.pageButton) st.lastPage = (st.lastPage + 1) % 2;
  bool fxPage = st.lastPage == 1;

  dash::CadenceRules rules;
  int minOfDay = 0;
  auto updateClock = [&]() {
    timeOk = time_sync::timeValid();
    now = time_sync::nowEpoch();
    if (timeOk) {
      time_t t = now;
      struct tm local;
      localtime_r(&t, &local);
      minOfDay = local.tm_hour * 60 + local.tm_min;
    }
    rules.night = cfg.nightEnabled && timeOk &&
                  dash::isNight(minOfDay, cfg.nightStartMin, cfg.nightEndMin);
    rules.nightIntervalS = cfg.nightIntervalS;
    rules.lowBattery = lowBatt;
    rules.wifiFails = st.consecWifiFails;
  };
  updateClock();

  // --- cached data (every wake renders, online or not) ------------------
  static screen::Frame frame;
  frame = screen::Frame();
  frame.page = fxPage ? screen::PAGE_FX : screen::PAGE_POWER;
  PowerView& view = frame.power;
  view.widgetWeather = cfg.widgetWeather;
  view.widgetOutage = cfg.widgetOutage;
  view.widgetBackup = cfg.widgetBackup;
  bool deye = cfg.widgetBackup && cfg.hasDeye();

  if (cfg.widgetWeather) view.hasWeather = state_store::loadBlob(SRC_WEATHER, &view.weather, sizeof(view.weather));
  if (deye) {
    view.hasBackup = state_store::loadBlob(SRC_BACKUP, &view.backup, sizeof(view.backup));
    view.hasSoc = state_store::loadBlob(SRC_SOC, &view.soc, sizeof(view.soc));
  }
  if (!view.hasSoc) memset(&view.soc, 0, sizeof(view.soc));
  if (fxPage) frame.hasFx = state_store::loadBlob(SRC_FX, &frame.fx, sizeof(frame.fx));

  bool groupChanged = false;
  if (cfg.widgetOutage) {
    strlcpy(view.outageGroup, cfg.yasnoGroup.c_str(), sizeof(view.outageGroup));
    view.hasOutage = state_store::loadBlob(SRC_OUTAGE, &view.outage, sizeof(view.outage));
    if (view.hasOutage && strcmp(view.outage.groupId, view.outageGroup) != 0) {
      // Group was changed in the portal: the cache belongs to the old group.
      // Never show another group's schedule; fetch now.
      view.hasOutage = false;
      groupChanged = true;
    }
  }

  // --- which tasks are due ------------------------------------------------
  Plan plan;
  planTasks(cfg, st, rs, rules, fxPage, now, plan);
  bool forceAll = wake.refreshButton || wake.coldBoot || !timeOk;
  if (forceAll) {
    for (int i = 0; i < SRC_COUNT; i++) {
      if (plan.interval[i]) plan.dueAt[i] = now;
    }
  }
  if (groupChanged) plan.dueAt[SRC_OUTAGE] = now;

  bool anyDue = false;
  for (int i = 0; i < SRC_COUNT; i++) anyDue |= dash::taskDue(plan.dueAt[i], now, kDueSlackS);

  bool battOkForOta = vbat < 0.5f || vbat >= DEF_VBAT_OTA_MIN;  // < 0.5: unknown
  uint32_t otaPeriodS = (uint32_t)cfg.otaIntervalH * 3600;
  bool otaDue = battOkForOta && !ota_pull::pendingVerify() &&
                (wake.coldBoot || (otaPeriodS && (st.lastOtaCheckEpoch == 0 ||
                                                  now < st.lastOtaCheckEpoch ||
                                                  now - st.lastOtaCheckEpoch >= otaPeriodS)));
  // A pending (fresh from OTA) image must get online to confirm itself; a
  // deep sleep before that would roll it back.
  bool goOnline = anyDue || otaDue || ota_pull::pendingVerify() || forceAll;
  LOGI("cycle", "%s wake%s%s", goOnline ? "online" : "offline", rules.night ? ", night" : "",
       lowBatt ? ", low battery" : "");

  // --- network ---------------------------------------------------------
  bool online = false;
  if (goOnline) {
    online = wifi_mgr::connect(cfg);
    if (online) {
      st.consecWifiFails = 0;
      // now < lastSntpEpoch (clock jumped back) wraps to a huge age -> resync.
      if (wake.coldBoot || !timeOk || st.lastSntpEpoch == 0 ||
          now - st.lastSntpEpoch > cfg.sntpIntervalS) {
        if (time_sync::syncSntp(cfg)) st.lastSntpEpoch = time_sync::nowEpoch();
      }
    } else {
      st.consecWifiFails++;
    }
    bool hadClock = timeOk;
    updateClock();
    if (!hadClock && timeOk) {
      // First valid time: the plan was made with a bogus clock.
      planTasks(cfg, st, rs, rules, fxPage, now, plan);
      for (int i = 0; i < SRC_COUNT; i++) {
        if (plan.interval[i]) plan.dueAt[i] = now;
      }
    }
  }

  // Without valid wall time the per-source age logic and all timestamps are
  // meaningless; only fetch when time is sane (first boot needs one SNTP).
  bool net = online && timeOk;
  if (goOnline && !online && timeOk) {
    // No WiFi: count it as an attempt for everything that was due, so the
    // next try comes one interval later instead of on every wake.
    for (int i = 0; i < SRC_COUNT; i++) {
      if (dash::taskDue(plan.dueAt[i], now, kDueSlackS)) rs.lastAttemptEpoch[i] = now;
    }
    if (otaDue) st.lastOtaCheckEpoch = now;
  }
  net::resetCounters();
  auto runs = [&](Source src) {
    return net && dash::taskDue(plan.dueAt[src], now, kPiggybackS);
  };
  auto attempted = [&](Source src) { rs.lastAttemptEpoch[src] = now; };
  auto succeeded = [&](Source src, const void* data, size_t size) {
    state_store::saveBlob(src, data, size);
    st.lastSuccessEpoch[src] = now;
  };

  if (runs(SRC_WEATHER)) {
    attempted(SRC_WEATHER);
    dash::WeatherData w;
    if (weather_api::fetch(cfg, w)) {
      view.weather = w;
      view.hasWeather = true;
      succeeded(SRC_WEATHER, &w, sizeof(w));
    }
  }
  if (runs(SRC_OUTAGE)) {
    attempted(SRC_OUTAGE);
    static dash::OutageSchedule o;
    yasno_api::FetchError err = yasno_api::ERR_NONE;
    if (yasno_api::fetch(cfg, o, &err)) {
      view.outage = o;
      view.hasOutage = true;
      rs.outageErr = yasno_api::ERR_NONE;
      succeeded(SRC_OUTAGE, &o, sizeof(o));
    } else {
      rs.outageErr = err;
    }
  }
  if (runs(SRC_BACKUP)) {
    attempted(SRC_BACKUP);
    dash::BackupData b;
    if (deye_api::fetchStatus(cfg, st, b)) {
      view.backup = b;
      view.hasBackup = true;
      succeeded(SRC_BACKUP, &b, sizeof(b));
    }
  }
  if (runs(SRC_SOC)) {
    attempted(SRC_SOC);
    if (deye_api::fetchSoc(cfg, st, view.soc)) {
      view.hasSoc = true;
      succeeded(SRC_SOC, &view.soc, sizeof(view.soc));
    }
  }
  if (runs(SRC_FX)) {
    attempted(SRC_FX);
    static dash::FxData fx;
    if (fx_api::fetch(cfg, fx)) {
      frame.fx = fx;
      frame.hasFx = true;
      succeeded(SRC_FX, &fx, sizeof(fx));
    }
  }

  // --- connectivity -----------------------------------------------------
  if (goOnline) {
    const net::Counters& c = net::counters();
    rs.connectivity = dash::classifyConnectivity(online, c.attempts, c.responses,
                                                 c.transportErrors,
                                                 (dash::Connectivity)rs.connectivity);
    if (online && rs.connectivity == dash::CONN_UNKNOWN) rs.connectivity = dash::CONN_OK;
    LOGI("cycle", "requests %d, responses %d, transport errors %d -> connectivity %d",
         c.attempts, c.responses, c.transportErrors, rs.connectivity);
    if (timeOk && c.responses > 0) st.lastInternetEpoch = now;
  }
  if (timeOk) {
    if (online) st.lastOnlineEpoch = now;
    // Every wake at/above the "full" voltage restarts the counter, so it
    // effectively counts from when the device came off the charger.
    if (vbat >= cfg.vbatFull) st.lastFullEpoch = now;
  }

  // --- OTA check (before the panel refresh, while WiFi is up) -------------
  if (net && otaDue) {
    st.lastOtaCheckEpoch = now;
    ota_pull::Result ota = ota_pull::checkAndUpdate(cfg, st.otaBadVersion);
    if (ota.installed) {
      st.otaTriedVersion = ota.version;
      state_store::save(st);
      net::closeAll();
      wifi_mgr::disconnect();
      rtc_state::commit();
      ESP.restart();
    }
  }

  // Offline wakes keep their state in RTC RAM only (flash wear).
  if (goOnline || wake.pageButton) state_store::save(st);
  if (goOnline) {
    net::closeAll();
    wifi_mgr::disconnect();  // radio off before the (slow) panel refresh
  }

  // --- derived view -------------------------------------------------------
  if (view.hasOutage && timeOk) rollOverOutageDays(view.outage);
  if (cfg.widgetOutage && !view.hasOutage) outageErrorText(st, rs, timeOk, view);

  if (timeOk) {
    view.weatherStale = view.hasWeather &&
                        dash::isStale(st.lastSuccessEpoch[SRC_WEATHER], plan.interval[SRC_WEATHER], now);
    view.outageStale = view.hasOutage &&
                       dash::isStale(st.lastSuccessEpoch[SRC_OUTAGE], plan.interval[SRC_OUTAGE], now);
    view.backupStale = view.hasBackup &&
                       dash::isStale(st.lastSuccessEpoch[SRC_BACKUP], plan.interval[SRC_BACKUP], now);
    frame.fxStale = frame.hasFx &&
                    dash::isStale(st.lastSuccessEpoch[SRC_FX], plan.interval[SRC_FX], now);
  }

  // The logger lost its own connection: Deye cloud still answers, but with a
  // reading that was already old when we fetched it.
  bool inverterOffline = false;
  if (view.hasBackup && view.backup.lastUpdateEpoch && st.lastSuccessEpoch[SRC_BACKUP] >
                                                           view.backup.lastUpdateEpoch +
                                                               DEF_INVERTER_STALE_S) {
    inverterOffline = true;
    view.backupStale = true;
  }

  if (hasIndoor) {
    float t = indoorT + cfg.indoorTempOffset;
    float rh = constrain(indoorRh + cfg.indoorRhOffset, 0.0f, 100.0f);
    // Hysteresis on the shown integers: a reading hovering at x.5 must not
    // flip the digit (and cost a refresh) on every wake.
    int shownT = dash::stickyRound(t, rs.shownTemp, rs.hasShownIndoor, 0.8f);
    int shownRh = dash::stickyRound(rh, rs.shownRh, rs.hasShownIndoor, 1.5f);
    rs.shownTemp = (int16_t)shownT;
    rs.shownRh = (int16_t)shownRh;
    rs.hasShownIndoor = true;
    view.hasIndoor = true;
    view.indoorTemp = (float)shownT;
    view.indoorRh = (float)shownRh;
    view.indoorTempOut = shownT < cfg.comfortTempMin || shownT > cfg.comfortTempMax;
    view.indoorRhOut = shownRh < cfg.comfortRhMin || shownRh > cfg.comfortRhMax;
  }
  rs.lastIndoorEpoch = now;

  if (timeOk) {
    time_t t = now;
    struct tm local;
    localtime_r(&t, &local);
    snprintf(view.nowLocalIso, sizeof(view.nowLocalIso), "%04d-%02d-%02dT%02d:%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
             local.tm_min);
  }

  buildStatusBar(cfg, st, rs, frame, timeOk, lowBatt, vbat, inverterOffline, frame.bar);

  // --- panel -----------------------------------------------------------------
  screen::Policy policy;
  policy.now = timeOk ? now : 0;
  policy.forceFull = wake.coldBoot || wake.refreshButton;
  policy.night = rules.night;
  policy.fullRefreshMin = cfg.fullRefreshMin;
  policy.maxPartials = cfg.maxPartials;
  policy.cold = hasIndoor && indoorT + cfg.indoorTempOffset < kColdPanelC;
  screen::present(frame, policy);

  // A freshly updated image that rendered and got online is healthy; until
  // then any reset rolls it back (main.cpp retries before giving up).
  if (online) ota_pull::confirmIfPending();

  // --- next wake ---------------------------------------------------------------
  uint32_t sleepS;
  if (!timeOk) {
    sleepS = kNoClockSleepS * (st.consecWifiFails >= 3 ? 2 : 1);
  } else {
    Plan next;
    planTasks(cfg, st, rs, rules, fxPage, now, next);
    uint32_t wakeAt = dash::kNever;
    const char* reason = "max sleep";
    auto consider = [&](uint32_t at, const char* why) {
      if (at < wakeAt) {
        wakeAt = at;
        reason = why;
      }
    };
    static const char* kNames[SRC_COUNT] = {"weather", "outage", "inverter", "fx", "soc"};
    for (int i = 0; i < SRC_COUNT; i++) consider(next.dueAt[i], kNames[i]);

    uint32_t indoorS = dash::effectiveInterval(cfg.indoorIntervalS, false, rules);
    if (hasIndoor && indoorS) consider(rs.lastIndoorEpoch + indoorS, "indoor");
    if (otaPeriodS) consider(st.lastOtaCheckEpoch + otaPeriodS, "update check");

    time_t t = now;
    struct tm local;
    localtime_r(&t, &local);
    uint32_t midnight = now - (uint32_t)(minOfDay * 60 + local.tm_sec);
    if (view.hasOutage) {
      int b = dash::nextOutageBoundaryMin(view.outage, minOfDay);
      if (b >= 0) consider(midnight + (uint32_t)b * 60 + kEventLagS, "outage start/end");
    }
    if (cfg.nightEnabled) {
      int m = dash::minutesToNightBoundary(minOfDay, cfg.nightStartMin, cfg.nightEndMin);
      if (m > 0) consider(midnight + (uint32_t)(minOfDay + m) * 60 + kEventLagS, "night edge");
    }
    // Forecast rows and the date roll over on the hour (Kyiv is a whole-hour
    // offset from UTC).
    consider((now / 3600 + 1) * 3600 + kEventLagS, "full hour");

    sleepS = dash::sleepSeconds(wakeAt, now, kMinSleepS, kMaxSleepS);
    LOGI("cycle", "next wake in %lus (%s)", (unsigned long)sleepS, reason);
  }

  rtc_state::commit();
  return sleepS;
}

}  // namespace wake_cycle
