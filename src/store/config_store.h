// Runtime configuration, persisted in NVS namespace "cfg".
// Every field is editable through the maintenance portal - changing behavior
// (intervals, group, credentials, widgets) requires no reflash.
#pragma once

#include <Arduino.h>

struct Config {
  String wifiSsid;
  String wifiPass;
  String tz;

  uint32_t wifiTimeoutMs;
  uint32_t sntpIntervalS;
  uint16_t otaIntervalH;  // 0 = no periodic update check

  uint32_t weatherMaxAgeS;
  uint32_t outageMaxAgeS;
  uint32_t backupMaxAgeS;  // Deye status tiles
  uint32_t socMaxAgeS;     // Deye SOC graph
  uint32_t fxMaxAgeS;
  uint32_t alertMaxAgeS;
  uint32_t indoorIntervalS;

  bool nightEnabled;
  uint16_t nightStartMin;  // minutes since local midnight
  uint16_t nightEndMin;
  uint32_t nightIntervalS;

  uint16_t fullRefreshMin;  // 0 = no time-based full refresh
  uint16_t maxPartials;     // 0 = no count-based full refresh

  float vbatLow;
  float vbatCrit;

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

  String alertApiKey;   // api.ukrainealarm.com key, sent as the Authorization header
  String alertRegions;  // comma-separated region ids (max 3), e.g. "31"

  String otaManifestUrl;

  bool widgetWeather;
  bool widgetOutage;
  bool widgetBackup;
  bool widgetAlerts;
  bool stayAwakeOnUsb;

  bool hasWifi() const { return wifiSsid.length() > 0; }
  bool hasAlerts() const { return widgetAlerts && alertApiKey.length() && alertRegions.length(); }
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
