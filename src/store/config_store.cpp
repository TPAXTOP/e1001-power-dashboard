#include "config_store.h"

#include <Preferences.h>
#include <nvs_flash.h>

#include "../../include/defaults.h"

namespace config_store {

static const char* kNs = "cfg";

// Preferences::getFloat() logs an [E] line for every key that was never
// saved (defaults are the norm), so check presence first.
static float getFloatOr(Preferences& p, const char* key, float def) {
  return p.isKey(key) ? p.getFloat(key, def) : def;
}

void load(Config& cfg) {
  Preferences p;
  p.begin(kNs, true);

  cfg.wifiSsid = p.getString("wifi_ssid", "");
  cfg.wifiPass = p.getString("wifi_pass", "");
  cfg.tz = p.getString("tz", DEF_TZ);

  cfg.wifiTimeoutMs = p.getUInt("wifi_to_ms", DEF_WIFI_TIMEOUT_MS);
  cfg.sntpIntervalS = p.getUInt("iv_sntp", DEF_SNTP_INTERVAL_S);
  cfg.otaIntervalH = p.getUShort("ota_hours", DEF_OTA_INTERVAL_H);

  // New key names for the intervals whose defaults changed in 0.4.0, so a
  // value saved by an older portal (which always saved every field) does not
  // pin the old 5-min / 10-min cadence.
  cfg.weatherMaxAgeS = p.getUInt("age_weather", DEF_WEATHER_MAX_AGE_S);
  cfg.outageMaxAgeS = p.getUInt("iv_outage", DEF_OUTAGE_MAX_AGE_S);
  cfg.backupMaxAgeS = p.getUInt("iv_backup", DEF_BACKUP_MAX_AGE_S);
  cfg.socMaxAgeS = p.getUInt("iv_soc", DEF_SOC_MAX_AGE_S);
  cfg.fxMaxAgeS = p.getUInt("age_fx", DEF_FX_MAX_AGE_S);
  cfg.indoorIntervalS = p.getUInt("iv_indoor", DEF_INDOOR_INTERVAL_S);

  cfg.nightEnabled = p.getBool("night_on", DEF_NIGHT_ENABLED);
  cfg.nightStartMin = p.getUShort("night_start", DEF_NIGHT_START_MIN);
  cfg.nightEndMin = p.getUShort("night_end", DEF_NIGHT_END_MIN);
  cfg.nightIntervalS = p.getUInt("iv_night", DEF_NIGHT_INTERVAL_S);

  cfg.fullRefreshMin = p.getUShort("full_min", DEF_FULL_REFRESH_MIN);
  cfg.maxPartials = p.getUShort("max_partial", DEF_MAX_PARTIALS);

  cfg.vbatLow = getFloatOr(p, "vbat_low", DEF_VBAT_LOW);
  cfg.vbatCrit = getFloatOr(p, "vbat_crit", DEF_VBAT_CRITICAL);
  cfg.vbatFull = getFloatOr(p, "vbat_full", DEF_VBAT_FULL);

  cfg.indoorTempOffset = getFloatOr(p, "in_t_off", DEF_INDOOR_T_OFFSET);
  cfg.indoorRhOffset = getFloatOr(p, "in_rh_off", DEF_INDOOR_RH_OFFSET);
  cfg.comfortTempMin = getFloatOr(p, "cf_t_min", DEF_COMFORT_T_MIN);
  cfg.comfortTempMax = getFloatOr(p, "cf_t_max", DEF_COMFORT_T_MAX);
  cfg.comfortRhMin = getFloatOr(p, "cf_rh_min", DEF_COMFORT_RH_MIN);
  cfg.comfortRhMax = getFloatOr(p, "cf_rh_max", DEF_COMFORT_RH_MAX);

  cfg.weatherLat = p.getString("wx_lat", DEF_WEATHER_LAT);
  cfg.weatherLon = p.getString("wx_lon", DEF_WEATHER_LON);

  cfg.yasnoUrl = p.getString("yasno_url", DEF_YASNO_URL);
  cfg.yasnoGroup = p.getString("yasno_group", DEF_YASNO_GROUP);

  cfg.deyeApiUrl = p.getString("deye_url", DEF_DEYE_API_URL);
  cfg.deyeAppId = p.getString("deye_app_id", "");
  cfg.deyeAppSecret = p.getString("deye_secret", "");
  cfg.deyeEmail = p.getString("deye_email", "");
  cfg.deyePasswordSha256 = p.getString("deye_pw_sha", "");
  cfg.deyeDeviceSn = p.getString("deye_sn", "");
  cfg.deyeBattWh = p.getUInt("deye_batt_wh", DEF_DEYE_BATT_WH);

  cfg.fxApiKey = p.getString("fx_key", "");
  cfg.fxBase = p.getString("fx_base", DEF_FX_BASE);
  cfg.fxTarget = p.getString("fx_target", DEF_FX_TARGET);
  cfg.fxHistoryDays = p.getUShort("fx_days", DEF_FX_HISTORY_DAYS);

  cfg.otaManifestUrl = p.getString("ota_url", DEF_OTA_MANIFEST_URL);
  // Before 0.3.0 the default was empty, and a portal save stored that.
  if (!cfg.otaManifestUrl.length()) cfg.otaManifestUrl = DEF_OTA_MANIFEST_URL;

  cfg.widgetWeather = p.getBool("w_weather", true);
  cfg.widgetOutage = p.getBool("w_outage", true);
  cfg.widgetBackup = p.getBool("w_backup", true);
  cfg.stayAwakeOnUsb = p.getBool("usb_awake", false);

  p.end();
}

void save(const Config& cfg) {
  Preferences p;
  p.begin(kNs, false);

  p.putString("wifi_ssid", cfg.wifiSsid);
  p.putString("wifi_pass", cfg.wifiPass);
  p.putString("tz", cfg.tz);

  p.putUInt("wifi_to_ms", cfg.wifiTimeoutMs);
  p.putUInt("iv_sntp", cfg.sntpIntervalS);
  p.putUShort("ota_hours", cfg.otaIntervalH);

  p.putUInt("age_weather", cfg.weatherMaxAgeS);
  p.putUInt("iv_outage", cfg.outageMaxAgeS);
  p.putUInt("iv_backup", cfg.backupMaxAgeS);
  p.putUInt("iv_soc", cfg.socMaxAgeS);
  p.putUInt("age_fx", cfg.fxMaxAgeS);
  p.putUInt("iv_indoor", cfg.indoorIntervalS);

  p.putBool("night_on", cfg.nightEnabled);
  p.putUShort("night_start", cfg.nightStartMin);
  p.putUShort("night_end", cfg.nightEndMin);
  p.putUInt("iv_night", cfg.nightIntervalS);

  p.putUShort("full_min", cfg.fullRefreshMin);
  p.putUShort("max_partial", cfg.maxPartials);

  p.putFloat("vbat_low", cfg.vbatLow);
  p.putFloat("vbat_crit", cfg.vbatCrit);
  p.putFloat("vbat_full", cfg.vbatFull);

  p.putFloat("in_t_off", cfg.indoorTempOffset);
  p.putFloat("in_rh_off", cfg.indoorRhOffset);
  p.putFloat("cf_t_min", cfg.comfortTempMin);
  p.putFloat("cf_t_max", cfg.comfortTempMax);
  p.putFloat("cf_rh_min", cfg.comfortRhMin);
  p.putFloat("cf_rh_max", cfg.comfortRhMax);

  p.putString("wx_lat", cfg.weatherLat);
  p.putString("wx_lon", cfg.weatherLon);

  p.putString("yasno_url", cfg.yasnoUrl);
  p.putString("yasno_group", cfg.yasnoGroup);

  p.putString("deye_url", cfg.deyeApiUrl);
  p.putString("deye_app_id", cfg.deyeAppId);
  p.putString("deye_secret", cfg.deyeAppSecret);
  p.putString("deye_email", cfg.deyeEmail);
  p.putString("deye_pw_sha", cfg.deyePasswordSha256);
  p.putString("deye_sn", cfg.deyeDeviceSn);
  p.putUInt("deye_batt_wh", cfg.deyeBattWh);

  p.putString("fx_key", cfg.fxApiKey);
  p.putString("fx_base", cfg.fxBase);
  p.putString("fx_target", cfg.fxTarget);
  p.putUShort("fx_days", cfg.fxHistoryDays);

  p.putString("ota_url", cfg.otaManifestUrl);

  p.putBool("w_weather", cfg.widgetWeather);
  p.putBool("w_outage", cfg.widgetOutage);
  p.putBool("w_backup", cfg.widgetBackup);
  p.putBool("usb_awake", cfg.stayAwakeOnUsb);

  p.end();
}

void factoryReset() {
  nvs_flash_erase();
  nvs_flash_init();
}

}  // namespace config_store
