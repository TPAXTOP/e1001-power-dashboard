#include "config_store.h"

#include <Preferences.h>
#include <nvs_flash.h>

#include "../../include/defaults.h"

namespace config_store {

static const char* kNs = "cfg";

void load(Config& cfg) {
  Preferences p;
  p.begin(kNs, true);

  cfg.wifiSsid = p.getString("wifi_ssid", "");
  cfg.wifiPass = p.getString("wifi_pass", "");
  cfg.tz = p.getString("tz", DEF_TZ);

  cfg.wakeIntervalS = p.getUInt("iv_wake", DEF_WAKE_INTERVAL_S);
  cfg.wifiTimeoutMs = p.getUInt("wifi_to_ms", DEF_WIFI_TIMEOUT_MS);
  cfg.sntpIntervalS = p.getUInt("iv_sntp", DEF_SNTP_INTERVAL_S);
  cfg.otaEveryN = p.getUShort("ota_every", DEF_OTA_EVERY_N_WAKES);

  cfg.weatherMaxAgeS = p.getUInt("age_weather", DEF_WEATHER_MAX_AGE_S);
  cfg.outageMaxAgeS = p.getUInt("age_outage", DEF_OUTAGE_MAX_AGE_S);
  cfg.backupMaxAgeS = p.getUInt("age_backup", DEF_BACKUP_MAX_AGE_S);
  cfg.fxMaxAgeS = p.getUInt("age_fx", DEF_FX_MAX_AGE_S);

  cfg.vbatLow = p.getFloat("vbat_low", DEF_VBAT_LOW);
  cfg.vbatCrit = p.getFloat("vbat_crit", DEF_VBAT_CRITICAL);
  cfg.vbatFull = p.getFloat("vbat_full", DEF_VBAT_FULL);

  cfg.indoorTempOffset = p.getFloat("in_t_off", DEF_INDOOR_T_OFFSET);
  cfg.indoorRhOffset = p.getFloat("in_rh_off", DEF_INDOOR_RH_OFFSET);
  cfg.comfortTempMin = p.getFloat("cf_t_min", DEF_COMFORT_T_MIN);
  cfg.comfortTempMax = p.getFloat("cf_t_max", DEF_COMFORT_T_MAX);
  cfg.comfortRhMin = p.getFloat("cf_rh_min", DEF_COMFORT_RH_MIN);
  cfg.comfortRhMax = p.getFloat("cf_rh_max", DEF_COMFORT_RH_MAX);

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

  p.putUInt("iv_wake", cfg.wakeIntervalS);
  p.putUInt("wifi_to_ms", cfg.wifiTimeoutMs);
  p.putUInt("iv_sntp", cfg.sntpIntervalS);
  p.putUShort("ota_every", cfg.otaEveryN);

  p.putUInt("age_weather", cfg.weatherMaxAgeS);
  p.putUInt("age_outage", cfg.outageMaxAgeS);
  p.putUInt("age_backup", cfg.backupMaxAgeS);
  p.putUInt("age_fx", cfg.fxMaxAgeS);

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
