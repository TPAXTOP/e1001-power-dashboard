// Runtime configuration, persisted in NVS namespace "cfg".
// Every field is editable through the maintenance portal - changing behavior
// (intervals, group, credentials, widgets) requires no reflash.
#pragma once

#include <Arduino.h>

struct Config {
  String wifiSsid;
  String wifiPass;
  String tz;

  uint32_t wakeIntervalS;
  uint32_t wifiTimeoutMs;
  uint32_t sntpIntervalS;
  uint16_t otaEveryN;

  uint32_t weatherMaxAgeS;
  uint32_t outageMaxAgeS;
  uint32_t backupMaxAgeS;
  uint32_t fxMaxAgeS;

  float vbatLow;
  float vbatCrit;
  float vbatFull;

  float indoorTempOffset;
  float indoorRhOffset;
  float comfortTempMin;
  float comfortTempMax;
  float comfortRhMin;
  float comfortRhMax;

  String weatherLat;
  String weatherLon;

  String yasnoUrl;
  String yasnoGroup;

  String deyeApiUrl;
  String deyeAppId;
  String deyeAppSecret;
  String deyeEmail;
  String deyePasswordSha256;  // SHA256 hex of the account password
  String deyeDeviceSn;
  uint32_t deyeBattWh;

  String fxApiKey;
  String fxBase;
  String fxTarget;
  uint16_t fxHistoryDays;

  String otaManifestUrl;

  bool widgetWeather;
  bool widgetOutage;
  bool widgetBackup;
  bool stayAwakeOnUsb;

  bool hasWifi() const { return wifiSsid.length() > 0; }
  bool hasDeye() const {
    return deyeAppId.length() && deyeAppSecret.length() && deyeEmail.length() &&
           deyePasswordSha256.length() && deyeDeviceSn.length();
  }
};

namespace config_store {

void load(Config& cfg);
void save(const Config& cfg);
void factoryReset();

}  // namespace config_store
