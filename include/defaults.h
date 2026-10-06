// Compile-time defaults for every runtime-configurable setting.
// Actual values live in NVS (namespace "cfg") and are editable without
// reflashing via the maintenance portal; these defaults seed first boot.
#pragma once

#define APP_NAME "eink-dash"

// POSIX TZ for Europe/Kyiv with EU DST rules
#define DEF_TZ "EET-2EEST,M3.5.0/3,M10.5.0/4"

// Wake cadence (seconds). Battery-first design: full deep sleep between wakes.
#define DEF_WAKE_INTERVAL_S 600         // 10 min; 300 for fresher outage data
#define DEF_WIFI_TIMEOUT_MS 12000
#define DEF_SNTP_INTERVAL_S 21600       // resync wall clock every 6 h
#define DEF_OTA_EVERY_N_WAKES 72        // ~12 h at the 10-min default

// Per-source max ages (seconds) - a source is only re-fetched when its last
// success is older than this, regardless of wake cadence.
#define DEF_WEATHER_MAX_AGE_S 1800
#define DEF_OUTAGE_MAX_AGE_S 300
#define DEF_BACKUP_MAX_AGE_S 300
#define DEF_FX_MAX_AGE_S 43200

// Battery policy (volts at the cell, after the ADC divider correction)
#define DEF_VBAT_LOW 3.45f              // stretch wake interval x4 below this
#define DEF_VBAT_CRITICAL 3.30f         // render "battery empty", sleep until button
#define DEF_VBAT_FULL 4.15f             // at/above this a wake counts as "fully charged"

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

// OTA manifest URL, e.g. https://github.com/<user>/<repo>/releases/latest/download/version.json
// Empty disables the periodic pull check.
#define DEF_OTA_MANIFEST_URL ""

#define MAINTENANCE_TIMEOUT_MS (10UL * 60UL * 1000UL)
#define BTN_HOLD_MS 1500
