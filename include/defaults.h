// Compile-time defaults for every runtime-configurable setting.
// Actual values live in NVS (namespace "cfg") and are editable without
// reflashing via the maintenance portal; these defaults seed first boot.
#pragma once

#define APP_NAME "eink-dash"

// POSIX TZ for Europe/Kyiv with EU DST rules
#define DEF_TZ "EET-2EEST,M3.5.0/3,M10.5.0/4"

// Wake cadence. The device wakes on one grid: every N seconds, where N is the
// shortest effective interval among the enabled sources and the indoor
// sensor (below), aligned to the clock (:00, :03, ... for 3 min). Each wake
// runs whatever is due by then. Deep sleep between wakes.
#define DEF_WIFI_TIMEOUT_MS 12000
#define DEF_SNTP_INTERVAL_S 21600       // resync wall clock every 6 h
#define DEF_OTA_INTERVAL_H 12           // update check (plus every cold boot); 0 = never

// Per-source refresh intervals (seconds). WiFi is only switched on when one of
// them is due on a wake; a source runs on the wake closest to its due time.
#define DEF_WEATHER_MAX_AGE_S 1800
#define DEF_OUTAGE_MAX_AGE_S 600
#define DEF_BACKUP_MAX_AGE_S 180        // Deye status tiles (battery, grid, charge, load)
#define DEF_SOC_MAX_AGE_S 900           // Deye 24 h SOC graph (incremental fetch)
#define DEF_FX_MAX_AGE_S 43200
#define DEF_ALERT_MAX_AGE_S 180         // air raid alerts
#define DEF_INDOOR_INTERVAL_S 180       // SHT4x re-read; no WiFi needed

// Night window (minutes since local midnight): every task runs at most once
// per DEF_NIGHT_INTERVAL_S, and no hourly full refresh.
#define DEF_NIGHT_ENABLED true
#define DEF_NIGHT_START_MIN 180         // 03:00
#define DEF_NIGHT_END_MIN 480           // 08:00
#define DEF_NIGHT_INTERVAL_S 900

// Panel refresh: changed frames use a fast partial refresh (no flashing); a
// full refresh clears ghosting at most this often (0 = only on other triggers).
#define DEF_FULL_REFRESH_MIN 360
#define DEF_MAX_PARTIALS 0              // full refresh after this many partials (0 = no limit)

// Inverter data older than this while Deye cloud answers = the logger is offline.
#define DEF_INVERTER_STALE_S 600

// Battery policy (volts at the cell, after the ADC divider correction)
#define DEF_VBAT_LOW 3.45f              // stretch wake interval x4 below this
#define DEF_VBAT_CRITICAL 3.30f         // render "battery empty", sleep until button
#define DEF_VBAT_OTA_MIN 3.70f          // no firmware download below this

// Indoor climate (onboard SHT4x). Offsets correct for the warm enclosure;
// values outside the comfort range are drawn inverted.
#define DEF_INDOOR_T_OFFSET 0.0f
#define DEF_INDOOR_RH_OFFSET 0.0f
#define DEF_COMFORT_T_MIN 18.0f
#define DEF_COMFORT_T_MAX 26.0f
#define DEF_COMFORT_RH_MIN 30.0f
#define DEF_COMFORT_RH_MAX 60.0f

// Data sources
#define DEF_YASNO_URL \
  "https://app.yasno.ua/api/blackout-service/public/shutdowns/regions/25/dsos/902/planned-outages"
#define DEF_YASNO_GROUP "1.1"
#define DEF_WEATHER_LAT "50.45"
#define DEF_WEATHER_LON "30.52"
#define DEF_DEYE_API_URL "https://eu1-developer.deyecloud.com"
#define DEF_DEYE_BATT_WH 5120
#define DEF_FX_BASE "USD"
#define DEF_FX_TARGET "UAH"
#define DEF_FX_HISTORY_DAYS 29
// Air raid alerts: api.ukrainealarm.com v3 (API key from the portal).
// Region ids from /api/v3/regions; 31 = Kyiv city.
#define DEF_ALERT_API_URL "https://api.ukrainealarm.com"
#define DEF_ALERT_REGIONS "31"

// OTA manifest of the latest published (non-pre)release. Images must be signed
// with the key matching certs/ota_signing_pub.pem. An empty URL in NVS falls
// back to this; "update check every N wakes" = 0 disables the pull check.
#define DEF_OTA_MANIFEST_URL \
  "https://github.com/TPAXTOP/e1001-power-dashboard/releases/latest/download/version.json"

#define MAINTENANCE_TIMEOUT_MS (10UL * 60UL * 1000UL)
#define BTN_HOLD_MS 1500
