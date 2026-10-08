#include "wake_cycle.h"

#include <Arduino.h>
#include <alerts.h>
#include <schedule.h>
#include <status.h>
#include <string.h>
#include <time.h>

#include "../../include/defaults.h"
#include "../../include/version.h"
#include "../api/alert_api.h"
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
#include "../store/sd_log.h"
#include "../ui/display.h"
#include "../ui/render_system.h"
#include "../ui/screen.h"
#include "../util/log.h"
#include "power_mgmt.h"

namespace wake_cycle {

static const uint32_t kMinSleepS = 20;
static const uint32_t kMaxSleepS = dash::kMaxTickS + 60;
static const uint32_t kNoClockSleepS = 600;  // until SNTP works, wake on a plain timer
// Below this the fast partial waveform (fixed temperature) is out of spec.
static const float kColdPanelC = 12.0f;
// Wake a few seconds after the grid slot, so "now" is safely past it (the
// outage countdown flips to "Power off now" exactly on the slot).
static const uint32_t kSlotLagS = 5;

// api.ukrainealarm.com answers 401 not only for a bad key but also when the
// key is used too often, so one 401 proves nothing: only this many in a row
// (one per alert interval) count as "key rejected".
static const uint8_t kAlertAuthFailsShown = 5;
// ... and a request sooner than this after the previous one is refused for
// sure (seen: 401 at a 46 s gap, green button right after a scheduled check).
static const uint32_t kAlertMinGapS = 65;

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

// The wake grid (schedule.h): every enabled task's effective interval, the
// tick = the shortest of them (indoor sensor included), and the slot this
// wake belongs to. A task runs when it is due on this slot.
struct Plan {
  uint32_t interval[SRC_COUNT] = {};  // effective; 0 = source off
  uint32_t dueAt[SRC_COUNT] = {};
  uint32_t tick = dash::kMaxTickS;
  uint32_t slot = 0;

  bool due(int src) const { return dash::dueOnSlot(dueAt[src], slot, tick); }
};

static void planTasks(const Config& cfg, const PersistedState& st, const RtcState& rs,
                      const dash::CadenceRules& rules, bool fxPage, bool hasIndoor, uint32_t now,
                      Plan& plan) {
  uint32_t base[SRC_COUNT] = {};
  base[SRC_WEATHER] = cfg.widgetWeather ? cfg.weatherMaxAgeS : 0;
  base[SRC_OUTAGE] = cfg.widgetOutage ? cfg.outageMaxAgeS : 0;
  bool deye = cfg.widgetBackup && cfg.hasDeye();
  base[SRC_BACKUP] = deye ? cfg.backupMaxAgeS : 0;
  base[SRC_SOC] = deye ? cfg.socMaxAgeS : 0;
  base[SRC_FX] = fxPage ? cfg.fxMaxAgeS : 0;
  base[SRC_ALERT] = cfg.hasAlerts() ? cfg.alertMaxAgeS : 0;

  uint32_t all[SRC_COUNT + 1];
  for (int i = 0; i < SRC_COUNT; i++) {
    plan.interval[i] = dash::effectiveInterval(base[i], true, rules);
    plan.dueAt[i] =
        dash::taskDueAt(st.lastSuccessEpoch[i], rs.lastAttemptEpoch[i], plan.interval[i], now);
    all[i] = plan.interval[i];
  }
  // The indoor sensor is read on every wake; its interval only sets the tick.
  all[SRC_COUNT] = hasIndoor ? dash::effectiveInterval(cfg.indoorIntervalS, false, rules) : 0;
  plan.tick = dash::wakeTick(all, SRC_COUNT + 1);
  plan.slot = dash::gridSlot(now, plan.tick);
}

// --- status bar ------------------------------------------------------------------

struct StaleInfo {
  bool stale;
  Source src;
  const char* name;
};

struct AlertView {
  bool active = false;   // a fresh Red/Yellow alert: shown above everything
  bool stale = false;    // alert data too old to trust
  bool keyRejected = false;
  bool unavailable = false;  // tried, but no alert data ever arrived
  dash::AlertStatus status = {};
};

// "14:05", or "08.10" for an alert that started more than 20 h ago (the
// status bar reads "... з 14:05").
static void alertSince(uint32_t since, uint32_t now, char* buf, size_t len) {
  buf[0] = '\0';
  if (!since || since > now + 300) return;
  time_t t = since;
  struct tm local;
  localtime_r(&t, &local);
  if (now - since < 20 * 3600) {
    snprintf(buf, len, "%02d:%02d", local.tm_hour, local.tm_min);
  } else {
    snprintf(buf, len, "%02d.%02d", local.tm_mday, local.tm_mon + 1);
  }
}

// One message by priority (air raid alert > problems > outage countdown >
// low battery), plus the connectivity icon and the device battery. Every
// duration is quantized: the panel is only refreshed when the frame changes.
static void buildStatusBar(const Config& cfg, const PersistedState& st, RtcState& rs,
                           const screen::Frame& fr, const AlertView& alert, bool timeOk,
                           bool lowBatt, float vbat, bool charging, bool inverterOffline,
                           StatusBarView& sb) {
  const PowerView& view = fr.power;
  uint32_t now = time_sync::nowEpoch();
  char dur[12];
  auto age = [&](uint32_t since) {
    dash::formatDuration(dash::quantizeAge(now - since), dur, sizeof(dur));
    return dur;
  };

  const StaleInfo stale[] = {
      {alert.stale, SRC_ALERT, "Air alert"},
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
  if (alert.active) {
    sb.severity = alert.status.level == dash::ALERT_RED ? StatusBarView::SEV_ALERT_RED
                                                        : StatusBarView::SEV_ALERT_YELLOW;
    char since[16];  // formatAlert() appends " з <since>"
    alertSince(alert.status.sinceEpoch, now, since, sizeof(since));
    dash::formatAlert(alert.status, since, msg, len);
  } else if (!timeOk) {
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
  } else if (alert.keyRejected) {
    strlcpy(msg, "Air alert API key rejected, check settings", len);
  } else if (alert.unavailable) {
    strlcpy(msg, "No air alert data yet", len);
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
  sb.charging = charging;
  if (!charging && timeOk && st.chargeEndEpoch && now >= st.chargeEndEpoch) {
    sb.hasSinceCharge = true;
    sb.sinceChargeEstimated = st.chargeEndEstimated;
    sb.sinceChargeS = dash::quantizeAge(now - st.chargeEndEpoch);
  }
  // Card detect + the previous wake's write result (this wake's log is
  // written right before deep sleep, after the panel).
  sb.sdState = sd_log::state();
}

// The last thing on the panel when the battery dies: how long it ran, and
// on which firmware, so battery life can be compared across versions.
static void batteryEmpty(const Config& cfg, const PersistedState& st, float vbat) {
  time_sync::initFromRtc(cfg);
  uint32_t now = time_sync::nowEpoch();
  char ran[96] = "";
  if (time_sync::timeValid() && st.chargeEndEpoch && now >= st.chargeEndEpoch) {
    char dur[12], since[24];
    dash::formatDuration(now - st.chargeEndEpoch, dur, sizeof(dur));
    time_t t = st.chargeEndEpoch;
    struct tm local;
    localtime_r(&t, &local);
    snprintf(since, sizeof(since), "%02d.%02d %02d:%02d", local.tm_mday, local.tm_mon + 1,
             local.tm_hour, local.tm_min);
    if (st.chargeEndEstimated) {
      snprintf(ran, sizeof(ran), "Ran ~%s on battery (no charge seen; counted from %s)", dur,
               since);
    } else {
      snprintf(ran, sizeof(ran), "Ran %s on battery (charge ended %s)", dur, since);
    }
    LOGW("cycle", "battery empty at %.3fV: %s", vbat, ran);
  }
  display::begin();
  render_system::renderBatteryEmpty(vbat, ran, APP_VERSION);
  display::show();
  screen::invalidate();
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
  LOGI("cycle", "boot=%lu vbat=%.3fV usb=%d", st.bootCount, vbat, usb);

  if (!usb && vbat > 0.5f && vbat < cfg.vbatCrit) {
    batteryEmpty(cfg, st, vbat);
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
  auto updateClock = [&]() {
    timeOk = time_sync::timeValid();
    now = time_sync::nowEpoch();
    int minOfDay = 0;
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

  static alert_api::AlertCache alertCache;  // ~150 B, static like the other big blobs
  memset(&alertCache, 0, sizeof(alertCache));
  bool hasAlert =
      cfg.hasAlerts() && state_store::loadBlob(SRC_ALERT, &alertCache, sizeof(alertCache));
  bool alertRegionsChanged = false;
  if (hasAlert && strcmp(alertCache.regions, cfg.alertRegions.c_str()) != 0) {
    // Regions changed in the portal: never show another region's alert.
    hasAlert = false;
    alertRegionsChanged = true;
  }

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

  // --- which tasks are due on this slot --------------------------------------
  Plan plan;
  planTasks(cfg, st, rs, rules, fxPage, hasIndoor, now, plan);
  bool forceAll = wake.refreshButton || wake.coldBoot || !timeOk;
  if (forceAll) {
    for (int i = 0; i < SRC_COUNT; i++) {
      if (plan.interval[i]) plan.dueAt[i] = now;
    }
  }
  if (groupChanged) plan.dueAt[SRC_OUTAGE] = now;
  if (alertRegionsChanged && plan.interval[SRC_ALERT]) plan.dueAt[SRC_ALERT] = now;

  bool anyDue = false;
  for (int i = 0; i < SRC_COUNT; i++) anyDue |= plan.due(i);

  bool battOkForOta = vbat < 0.5f || vbat >= DEF_VBAT_OTA_MIN;  // < 0.5: unknown
  uint32_t otaPeriodS = (uint32_t)cfg.otaIntervalH * 3600;
  bool otaDue = battOkForOta && !ota_pull::pendingVerify() &&
                (wake.coldBoot || (otaPeriodS && (st.lastOtaCheckEpoch == 0 ||
                                                  now < st.lastOtaCheckEpoch ||
                                                  now - st.lastOtaCheckEpoch >= otaPeriodS)));
  // A pending (fresh from OTA) image must get online to confirm itself; a
  // deep sleep before that would roll it back.
  bool goOnline = anyDue || otaDue || ota_pull::pendingVerify() || forceAll;
  LOGI("cycle", "%s wake, tick %lus%s%s", goOnline ? "online" : "offline",
       (unsigned long)plan.tick, rules.night ? ", night" : "", lowBatt ? ", low battery" : "");

  // --- network ---------------------------------------------------------
  bool online = false;
  uint32_t wifiStartMs = 0, wifiJoinMs = 0, wifiOnMs = 0;  // CSV: radio time
  if (goOnline) {
    wifiStartMs = millis();
    online = wifi_mgr::connect(cfg);
    wifiJoinMs = millis() - wifiStartMs;
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
      planTasks(cfg, st, rs, rules, fxPage, hasIndoor, now, plan);
      for (int i = 0; i < SRC_COUNT; i++) {
        if (plan.interval[i]) plan.dueAt[i] = now;
      }
    }
  }

  // Tasks are stamped with their grid slot, so due times stay on the grid
  // instead of drifting by the few seconds each wake takes. A wake ahead of
  // its slot (button) stamps "now": a time in the future would read as a
  // wrong clock.
  const uint32_t stamp = plan.slot < now ? plan.slot : now;
  if (timeOk) {
    char dueList[64] = "";
    static const char* kNames[SRC_COUNT] = {"weather", "outage", "inverter", "fx", "soc", "alerts"};
    for (int i = 0; i < SRC_COUNT; i++) {
      if (!plan.due(i)) continue;
      strlcat(dueList, " ", sizeof(dueList));
      strlcat(dueList, kNames[i], sizeof(dueList));
    }
    time_t t = plan.slot;
    struct tm local;
    localtime_r(&t, &local);
    LOGI("cycle", "slot %02d:%02d, due:%s", local.tm_hour, local.tm_min,
         dueList[0] ? dueList : " nothing");
  }

  // Without valid wall time the per-source age logic and all timestamps are
  // meaningless; only fetch when time is sane (first boot needs one SNTP).
  bool net = online && timeOk;
  if (goOnline && !online && timeOk) {
    // No WiFi: count it as an attempt for everything that was due, so the
    // next try comes one interval later instead of on every wake.
    for (int i = 0; i < SRC_COUNT; i++) {
      if (plan.due(i)) rs.lastAttemptEpoch[i] = stamp;
    }
    if (otaDue) st.lastOtaCheckEpoch = now;
  }
  net::resetCounters();
  auto runs = [&](Source src) { return net && plan.due(src); };
  auto attempted = [&](Source src) { rs.lastAttemptEpoch[src] = stamp; };
  auto succeeded = [&](Source src, const void* data, size_t size) {
    state_store::saveBlob(src, data, size);
    st.lastSuccessEpoch[src] = stamp;
  };

  // Alerts first: the most time-critical source.
  int alertHttp = -999;  // CSV: not requested this wake
  uint32_t sinceAlertReq = rs.lastAlertRequestEpoch && now >= rs.lastAlertRequestEpoch
                               ? now - rs.lastAlertRequestEpoch
                               : UINT32_MAX;
  if (runs(SRC_ALERT) && sinceAlertReq < kAlertMinGapS) {
    // Not an attempt: it stays due, and the next slot checks it.
    LOGI("alert", "skipped: previous request only %lus ago", (unsigned long)sinceAlertReq);
  } else if (runs(SRC_ALERT)) {
    attempted(SRC_ALERT);
    static alert_api::AlertCache fresh;
    alert_api::FetchError err = alert_api::ERR_NONE;
    // For the SD log: is a 401 the "same key within a minute" limit? The
    // gap to the previous request (any wake, any reason) answers that.
    uint32_t reqAt = time_sync::nowEpoch();
    if (rs.lastAlertRequestEpoch && reqAt >= rs.lastAlertRequestEpoch) {
      LOGI("alert", "request %lus after the previous one",
           (unsigned long)(reqAt - rs.lastAlertRequestEpoch));
    } else {
      LOGI("alert", "request (no previous one in RTC memory)");
    }
    rs.lastAlertRequestEpoch = reqAt;
    if (alert_api::fetch(cfg, now, fresh, &err, &alertHttp)) {
      alertCache = fresh;
      hasAlert = true;
      rs.alertErr = alert_api::ERR_NONE;
      rs.alertAuthFails = 0;
      succeeded(SRC_ALERT, &fresh, sizeof(fresh));
    } else {
      rs.alertErr = err;
      if (err == alert_api::ERR_AUTH && rs.alertAuthFails < 255) rs.alertAuthFails++;
      LOGW("alert", "failed (HTTP %d), %u auth refusal(s) since the last success", alertHttp,
           rs.alertAuthFails);
    }
  }
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

  // --- device battery charge detection ------------------------------------
  // Wall-clock based (now - chargeEndEpoch), so it counts across offline
  // wakes and deep sleep alike; see dash::updateCharge.
  dash::ChargeState charge;
  charge.peakV = st.chargePeakV;
  charge.minV = st.chargeMinV;
  charge.endEpoch = st.chargeEndEpoch;
  charge.estimated = st.chargeEndEstimated;
  bool chargeChanged = false;
  if (timeOk) {
    if (online) st.lastOnlineEpoch = now;
    chargeChanged = dash::updateCharge(charge, vbat, now);
    st.chargePeakV = charge.peakV;
    st.chargeMinV = charge.minV;
    st.chargeEndEpoch = charge.endEpoch;
    st.chargeEndEstimated = charge.estimated;
    LOGI("cycle", "charge: %s, peak %.3fV, min %.3fV, ended %lus ago%s",
         charge.charging ? "charging" : "not charging", charge.peakV, charge.minV,
         charge.endEpoch && now >= charge.endEpoch ? (unsigned long)(now - charge.endEpoch) : 0UL,
         charge.estimated ? " (estimate: no charge seen yet)" : "");
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
      power_mgmt::restart();
    }
  }

  // Offline wakes keep their state in RTC RAM only (flash wear) - except a
  // charge in progress, which must survive a power loss.
  if (goOnline || wake.pageButton || chargeChanged) state_store::save(st);
  if (goOnline) {
    net::closeAll();
    wifi_mgr::disconnect();  // radio off before the (slow) panel refresh
    wifiOnMs = millis() - wifiStartMs;
  }

  // --- derived view -------------------------------------------------------
  if (view.hasOutage && timeOk) rollOverOutageDays(view.outage);
  if (cfg.widgetOutage && !view.hasOutage) outageErrorText(st, rs, timeOk, view);

  AlertView alertView;
  if (timeOk) {
    view.weatherStale = view.hasWeather &&
                        dash::isStale(st.lastSuccessEpoch[SRC_WEATHER], plan.interval[SRC_WEATHER], now);
    view.outageStale = view.hasOutage &&
                       dash::isStale(st.lastSuccessEpoch[SRC_OUTAGE], plan.interval[SRC_OUTAGE], now);
    view.backupStale = view.hasBackup &&
                       dash::isStale(st.lastSuccessEpoch[SRC_BACKUP], plan.interval[SRC_BACKUP], now);
    frame.fxStale = frame.hasFx &&
                    dash::isStale(st.lastSuccessEpoch[SRC_FX], plan.interval[SRC_FX], now);
    // An alert is only shown while its data is fresh: an old "alert" may be
    // long over, an old "all clear" may not be. Stale -> "Air alert data N old".
    alertView.stale = hasAlert &&
                      dash::isStale(st.lastSuccessEpoch[SRC_ALERT], plan.interval[SRC_ALERT], now);
    alertView.active =
        hasAlert && !alertView.stale && alertCache.status.level != dash::ALERT_NONE;
    alertView.status = alertCache.status;
  }
  alertView.keyRejected = cfg.hasAlerts() && rs.alertErr == alert_api::ERR_AUTH &&
                          rs.alertAuthFails >= kAlertAuthFailsShown;
  alertView.unavailable = cfg.hasAlerts() && !hasAlert && rs.lastAttemptEpoch[SRC_ALERT];

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

  if (timeOk) {
    time_t t = now;
    struct tm local;
    localtime_r(&t, &local);
    snprintf(view.nowLocalIso, sizeof(view.nowLocalIso), "%04d-%02d-%02dT%02d:%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
             local.tm_min);
  }

  buildStatusBar(cfg, st, rs, frame, alertView, timeOk, lowBatt, vbat, charge.charging,
                 inverterOffline, frame.bar);

  // --- panel -----------------------------------------------------------------
  screen::Policy policy;
  policy.now = timeOk ? now : 0;
  policy.forceFull = wake.coldBoot || wake.refreshButton;
  policy.night = rules.night;
  policy.fullRefreshMin = cfg.fullRefreshMin;
  policy.maxPartials = cfg.maxPartials;
  policy.cold = hasIndoor && indoorT + cfg.indoorTempOffset < kColdPanelC;
  screen::Result shown = screen::present(frame, policy);

  // A freshly updated image that rendered and got online is healthy; until
  // then any reset rolls it back (main.cpp retries before giving up).
  if (online) ota_pull::confirmIfPending();

  // --- next wake: the next slot of the grid --------------------------------------
  uint32_t sleepS;
  if (!timeOk) {
    sleepS = kNoClockSleepS * (st.consecWifiFails >= 3 ? 2 : 1);
  } else {
    now = time_sync::nowEpoch();
    uint32_t next = plan.slot + plan.tick;
    // A wake that overran its tick (e.g. a slow update check) takes the
    // first slot still ahead instead of waking right away.
    if (next < now + kMinSleepS) next = (now / plan.tick + 1) * plan.tick;
    sleepS = dash::sleepSeconds(next + kSlotLagS, now, kMinSleepS, kMaxSleepS);
    time_t t = next;
    struct tm local;
    localtime_r(&t, &local);
    LOGI("cycle", "next wake %02d:%02d in %lus (tick %lus)", local.tm_hour, local.tm_min,
         (unsigned long)sleepS, (unsigned long)plan.tick);
  }

  // One CSV row per wake on the SD card (battery-life comparisons).
  static const char* const kCsvHeader =
      "time,fw,wake,vbat,batt_pct,charging,since_charge_h,since_charge_estimated,online,"
      "wifi_ok,wifi_join_ms,wifi_on_ms,requests,responses,transport_errors,alert_http,"
      "alert_auth_fails,screen,indoor_c,sleep_s";
  static const char* const kScreen[] = {"unchanged", "partial", "full"};
  const net::Counters& nc = net::counters();
  char when[20] = "";
  if (timeOk) {
    time_t t = now;
    struct tm local;
    localtime_r(&t, &local);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
  }
  char sinceH[12] = "", alertCol[8] = "", indoorCol[8] = "";
  if (timeOk && st.chargeEndEpoch && now >= st.chargeEndEpoch) {
    snprintf(sinceH, sizeof(sinceH), "%.2f", (now - st.chargeEndEpoch) / 3600.0f);
  }
  if (alertHttp != -999) snprintf(alertCol, sizeof(alertCol), "%d", alertHttp);
  if (hasIndoor) snprintf(indoorCol, sizeof(indoorCol), "%.1f", indoorT);
  char row[256];
  snprintf(row, sizeof(row), "%s,%s,%s,%.3f,%d,%d,%s,%d,%d,%d,%lu,%lu,%d,%d,%d,%s,%u,%s,%s,%lu",
           when, APP_VERSION,
           wake.coldBoot        ? "cold"
           : wake.refreshButton ? "refresh"
           : wake.pageButton    ? "page"
                                : "timer",
           vbat, dash::batteryPercentFromVolts(vbat), charge.charging ? 1 : 0, sinceH,
           st.chargeEndEstimated ? 1 : 0, goOnline ? 1 : 0, online ? 1 : 0,
           (unsigned long)wifiJoinMs, (unsigned long)wifiOnMs, nc.attempts, nc.responses,
           nc.transportErrors, alertCol, rs.alertAuthFails, kScreen[shown], indoorCol,
           (unsigned long)sleepS);
  sd_log::setWakeRow(kCsvHeader, row);

  rtc_state::commit();
  return sleepS;
}

}  // namespace wake_cycle
