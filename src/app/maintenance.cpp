#include "maintenance.h"

#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include <status.h>

#include "../../include/defaults.h"
#include "../../include/version.h"
#include "../hw/sht4x.h"
#include "../net/ota_pull.h"
#include "../net/time_sync.h"
#include "../store/state_store.h"
#include "../ui/display.h"
#include "../ui/render_system.h"
#include "../util/log.h"
#include "power_mgmt.h"

namespace maintenance {

static WebServer server(80);
static Config* gCfg = nullptr;
static uint32_t lastActivityMs = 0;
static bool rebootRequested = false;
static bool apMode = false;

static String sha256Hex(const String& input) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const uint8_t*)input.c_str(), input.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", hash[i]);
  return String(hex);
}

static String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

static void addText(String& html, const char* name, const char* label, const String& value,
                    const char* type = "text") {
  html += "<label>";
  html += label;
  html += "<input type='";
  html += type;
  html += "' name='";
  html += name;
  html += "' value='";
  html += htmlEscape(value);
  html += "'></label>";
}

static void addNum(String& html, const char* name, const char* label, uint32_t value) {
  addText(html, name, label, String(value), "number");
}

static void addFloat(String& html, const char* name, const char* label, float value) {
  html += "<label>";
  html += label;
  html += "<input type='number' step='0.01' name='";
  html += name;
  html += "' value='";
  html += String(value, 2);
  html += "'></label>";
}

// Live readings shown above the form (battery calibration, sensor offsets).
static String statusLine() {
  String s;
  float vbat = power_mgmt::batteryVolts();
  if (vbat > 0) {
    s += "Battery " + String(vbat, 2) + " V (" + String(dash::batteryPercentFromVolts(vbat)) + "%)";
  } else {
    s += "Battery: no valid reading";
  }
  PersistedState st;
  state_store::load(st);
  uint32_t now = time_sync::nowEpoch();
  if (time_sync::timeValid() && st.lastFullEpoch && now >= st.lastFullEpoch) {
    char dur[12];
    dash::formatDuration(now - st.lastFullEpoch, dur, sizeof(dur));
    s += ", last full ";
    s += dur;
    s += " ago";
  }
  if (st.otaBadVersion.length()) {
    s += "<br>Update to " + htmlEscape(st.otaBadVersion) +
         " was rolled back (it failed its first wake); it will not be retried automatically.";
  }
  float t, rh;
  if (sht4x::read(t, rh)) {
    s += "<br>Indoor sensor (raw, no offsets): " + String(t, 1) + " &deg;C, " + String(rh, 0) +
         "%. Reads warm while the portal's WiFi is on; calibrate against the dashboard value.";
  } else {
    s += "<br>Indoor sensor not responding";
  }
  return s;
}

static void addCheck(String& html, const char* name, const char* label, bool value) {
  html += "<label class='chk'><input type='checkbox' name='";
  html += name;
  html += "' value='1'";
  if (value) html += " checked";
  html += ">";
  html += label;
  html += "</label>";
}

static void handleRoot() {
  lastActivityMs = millis();
  const Config& c = *gCfg;

  String html;
  html.reserve(8192);
  html +=
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>eink-dash</title><style>"
      "body{font-family:system-ui;max-width:640px;margin:1em auto;padding:0 1em;background:#f4f4f4}"
      "h1{font-size:1.3em}h2{font-size:1.05em;margin:1.2em 0 .4em;border-bottom:2px solid #000}"
      "label{display:block;margin:.5em 0;font-size:.9em}"
      "input[type=text],input[type=password],input[type=number]{width:100%;padding:.4em;"
      "border:1px solid #999;border-radius:4px;box-sizing:border-box}"
      ".chk input{margin-right:.5em}button{padding:.6em 1.4em;font-size:1em;margin:.6em .4em 0 0}"
      "</style></head><body><h1>E-Paper Dashboard ";
  html += APP_VERSION;
  html += "</h1><p>";
  html += statusLine();
  html += "</p><form method='POST' action='/save'>";

  html += "<h2>WiFi</h2>";
  addText(html, "wifi_ssid", "SSID", c.wifiSsid);
  addText(html, "wifi_pass", "Password", c.wifiPass, "password");

  html += "<h2>Schedule (seconds)</h2>";
  addNum(html, "iv_wake", "Wake interval (refresh cadence)", c.wakeIntervalS);
  addNum(html, "age_outage", "Outage data max age", c.outageMaxAgeS);
  addNum(html, "age_backup", "Backup power max age", c.backupMaxAgeS);
  addNum(html, "age_weather", "Weather max age", c.weatherMaxAgeS);
  addNum(html, "age_fx", "FX max age", c.fxMaxAgeS);

  html += "<h2>Power outage (Yasno)</h2>";
  addText(html, "yasno_group",
          "Group, e.g. 29.1 (check your address at yasno.ua - Yasno renumbers groups)",
          c.yasnoGroup);

  html += "<h2>Deye inverter</h2>";
  addText(html, "deye_app_id", "App ID", c.deyeAppId);
  addText(html, "deye_secret", "App Secret", c.deyeAppSecret);
  addText(html, "deye_email", "Account email", c.deyeEmail);
  addText(html, "deye_pw_plain", "Account password (stored as SHA256 only)", "", "password");
  addText(html, "deye_pw_sha", "...or SHA256 hex directly", c.deyePasswordSha256);
  addText(html, "deye_sn", "Device serial number", c.deyeDeviceSn);
  addNum(html, "deye_batt_wh", "Battery capacity (Wh)", c.deyeBattWh);

  html += "<h2>Exchange rates</h2>";
  addText(html, "fx_key", "exchangerate.host API key", c.fxApiKey);

  html += "<h2>Updates</h2>";
  addText(html, "ota_url", "Update manifest URL (version.json; empty = project default)",
          c.otaManifestUrl);
  addNum(html, "ota_every", "Check for updates every N wakes (0 = never)", c.otaEveryN);

  html += "<h2>Indoor climate</h2>";
  addFloat(html, "in_t_off", "Temperature offset (&deg;C, added to the sensor)", c.indoorTempOffset);
  addFloat(html, "in_rh_off", "Humidity offset (%, added to the sensor)", c.indoorRhOffset);
  addFloat(html, "cf_t_min", "Comfortable temperature from (&deg;C)", c.comfortTempMin);
  addFloat(html, "cf_t_max", "Comfortable temperature to (&deg;C)", c.comfortTempMax);
  addFloat(html, "cf_rh_min", "Comfortable humidity from (%)", c.comfortRhMin);
  addFloat(html, "cf_rh_max", "Comfortable humidity to (%)", c.comfortRhMax);

  html += "<h2>Device battery</h2>";
  addFloat(html, "vbat_full",
           "Full-charge voltage (V). A wake at or above it restarts the \"since full\" timer",
           c.vbatFull);

  html += "<h2>Widgets & power</h2>";
  addCheck(html, "w_weather", "Weather widget", c.widgetWeather);
  addCheck(html, "w_outage", "Outage widget", c.widgetOutage);
  addCheck(html, "w_backup", "Backup power widget", c.widgetBackup);
  addCheck(html, "usb_awake", "Stay awake on USB power", c.stayAwakeOnUsb);

  html +=
      "<button type='submit'>Save & Reboot</button></form>"
      "<h2>Firmware update</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<label>firmware.bin<input type='file' name='fw' accept='.bin'></label>"
      "<button type='submit'>Upload & Flash</button></form>"
      "<form method='POST' action='/ota-check'><label>Download and install the latest signed "
      "release now (needs internet, takes up to a minute)</label>"
      "<button type='submit'>Check for update now</button></form>"
      "<form method='POST' action='/reboot'><button>Reboot now</button></form>"
      "</body></html>";

  server.send(200, "text/html", html);
}

static void handleSave() {
  lastActivityMs = millis();
  Config& c = *gCfg;

  // Trimmed: pasted credentials/group ids often carry stray whitespace.
  auto arg = [&](const char* name, const String& fallback) {
    String v = server.hasArg(name) ? server.arg(name) : fallback;
    v.trim();
    return v;
  };

  c.wifiSsid = arg("wifi_ssid", c.wifiSsid);
  // Not trimmed: spaces are legal in a WPA passphrase.
  if (server.hasArg("wifi_pass")) c.wifiPass = server.arg("wifi_pass");

  c.wakeIntervalS = arg("iv_wake", String(c.wakeIntervalS)).toInt();
  c.outageMaxAgeS = arg("age_outage", String(c.outageMaxAgeS)).toInt();
  c.backupMaxAgeS = arg("age_backup", String(c.backupMaxAgeS)).toInt();
  c.weatherMaxAgeS = arg("age_weather", String(c.weatherMaxAgeS)).toInt();
  c.fxMaxAgeS = arg("age_fx", String(c.fxMaxAgeS)).toInt();
  if (c.wakeIntervalS < 60) c.wakeIntervalS = 60;

  c.yasnoGroup = arg("yasno_group", c.yasnoGroup);

  c.deyeAppId = arg("deye_app_id", c.deyeAppId);
  c.deyeAppSecret = arg("deye_secret", c.deyeAppSecret);
  c.deyeEmail = arg("deye_email", c.deyeEmail);
  String plain = arg("deye_pw_plain", "");
  if (plain.length()) {
    c.deyePasswordSha256 = sha256Hex(plain);
  } else {
    c.deyePasswordSha256 = arg("deye_pw_sha", c.deyePasswordSha256);
  }
  c.deyeDeviceSn = arg("deye_sn", c.deyeDeviceSn);
  c.deyeBattWh = arg("deye_batt_wh", String(c.deyeBattWh)).toInt();

  c.fxApiKey = arg("fx_key", c.fxApiKey);
  c.otaManifestUrl = arg("ota_url", c.otaManifestUrl);
  c.otaEveryN = arg("ota_every", String(c.otaEveryN)).toInt();

  c.indoorTempOffset = arg("in_t_off", String(c.indoorTempOffset)).toFloat();
  c.indoorRhOffset = arg("in_rh_off", String(c.indoorRhOffset)).toFloat();
  c.comfortTempMin = arg("cf_t_min", String(c.comfortTempMin)).toFloat();
  c.comfortTempMax = arg("cf_t_max", String(c.comfortTempMax)).toFloat();
  c.comfortRhMin = arg("cf_rh_min", String(c.comfortRhMin)).toFloat();
  c.comfortRhMax = arg("cf_rh_max", String(c.comfortRhMax)).toFloat();
  float full = arg("vbat_full", String(c.vbatFull)).toFloat();
  if (full >= 3.9f && full <= 4.3f) c.vbatFull = full;  // a typo must not disable the timer

  // Unchecked checkboxes are absent from the POST body.
  c.widgetWeather = server.hasArg("w_weather");
  c.widgetOutage = server.hasArg("w_outage");
  c.widgetBackup = server.hasArg("w_backup");
  c.stayAwakeOnUsb = server.hasArg("usb_awake");

  config_store::save(c);
  server.send(200, "text/html",
              "<html><body><h2>Saved. Rebooting...</h2></body></html>");
  rebootRequested = true;
}

// Same signed pull as the wake cycle, but on demand and also willing to retry
// a version that was rolled back before.
static void handleOtaCheck() {
  lastActivityMs = millis();
  String msg;
  bool installed = false;
  if (apMode) {
    msg = "The device is in access-point mode (home WiFi unreachable), so it has no internet.";
  } else {
    ota_pull::Result r = ota_pull::checkAndUpdate(*gCfg, "");
    msg = r.message;
    if (r.installed) {
      PersistedState st;
      state_store::load(st);
      st.otaTriedVersion = r.version;
      st.otaBadVersion = "";
      state_store::save(st);
      installed = true;
    }
  }
  lastActivityMs = millis();
  server.send(200, "text/html",
              "<html><body><h2>" + htmlEscape(msg) + "</h2>" +
                  (installed ? String("<p>Rebooting into the new firmware...</p>")
                             : String("<p><a href='/'>Back</a></p>")) +
                  "</body></html>");
  if (installed) rebootRequested = true;
}

static void handleUpdateDone() {
  lastActivityMs = millis();
  if (Update.hasError()) {
    server.send(500, "text/plain", String("Update failed: ") + Update.errorString());
  } else {
    server.send(200, "text/html", "<html><body><h2>Flashed. Rebooting...</h2></body></html>");
    rebootRequested = true;
  }
}

static void handleUpdateUpload() {
  lastActivityMs = millis();
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    LOGI("maint", "OTA upload start: %s", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      LOGE("maint", "Update.begin: %s", Update.errorString());
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      LOGE("maint", "Update.write: %s", Update.errorString());
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      LOGI("maint", "OTA upload done: %u bytes", up.totalSize);
    } else {
      LOGE("maint", "Update.end: %s", Update.errorString());
    }
  }
}

void run(Config& cfg, bool provisioning) {
  gCfg = &cfg;
  // Reaching the portal is enough to accept a fresh image: from here any
  // firmware can be uploaded, so a rollback would only get in the way.
  ota_pull::confirmIfPending();
  time_sync::initFromRtc(cfg);  // for "last full ... ago" in the status line

  // AP credentials derived from the chip MAC: stable per device.
  uint64_t mac = ESP.getEfuseMac();
  char apSsid[20], apPass[16];
  snprintf(apSsid, sizeof(apSsid), "EINK-SETUP-%04X", (uint16_t)(mac >> 32));
  snprintf(apPass, sizeof(apPass), "eink%08X", (unsigned)(uint32_t)mac);

  String ip;
  apMode = provisioning;
  if (!provisioning) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < cfg.wifiTimeoutMs) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      ip = WiFi.localIP().toString();
    } else {
      apMode = true;  // fall back to AP when home WiFi is unreachable
    }
  }
  if (apMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid, apPass);
    ip = WiFi.softAPIP().toString();
  }

  MDNS.begin("eink");  // http://eink.local when the client supports mDNS

  display::begin();
  if (provisioning) {
    render_system::renderSetup(apSsid, apPass, ip.c_str());
  } else {
    render_system::renderMaintenance(ip.c_str(), apMode ? apSsid : nullptr, apPass);
  }
  display::show();
  display::hibernate();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/ota-check", HTTP_POST, handleOtaCheck);
  server.on("/reboot", HTTP_POST, []() {
    server.send(200, "text/plain", "rebooting");
    rebootRequested = true;
  });
  server.begin();
  LOGI("maint", "portal at http://%s (%s)", ip.c_str(), apMode ? apSsid : "STA");

  lastActivityMs = millis();
  while (true) {
    server.handleClient();
    delay(2);
    if (rebootRequested) {
      delay(500);  // let the response flush
      ESP.restart();
    }
    if (millis() - lastActivityMs > MAINTENANCE_TIMEOUT_MS) {
      LOGI("maint", "timeout, rebooting");
      ESP.restart();
    }
  }
}

}  // namespace maintenance
