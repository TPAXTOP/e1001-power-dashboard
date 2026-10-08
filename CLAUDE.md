# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Standalone ESP32-S3 firmware for the **Seeed Studio reTerminal E1001** (7.5" 800x480 monochrome ePaper, UC8179 controller). Recreates the `/power` page of the Next.js dashboard at `D:\WebProjects\eink-web-dashboard` (kept as the behavior/layout reference — do not modify it from here). The device fetches all data itself over HTTPS and deep-sleeps between refreshes.

Data sources: Open-Meteo (Kyiv weather), Yasno (power outage schedule, group-filtered), Deye Cloud (inverter/battery, token auth), api.ukrainealarm.com v3 (air raid alerts, API key in the `Authorization` header), exchangerate.host (USD/UAH, second page).

## Commands

PlatformIO and gcc are NOT on PATH on this machine:

```powershell
python -m platformio run -e e1001              # build firmware
python -m platformio run -e e1001 -t upload    # flash over USB-C
python -m platformio device monitor            # serial log, 115200 baud

# host unit tests need the portable MinGW first:
$env:PATH = "$env:LOCALAPPDATA\mingw-portable\mingw64\bin;$env:PATH"
python -m platformio test -e native
```

Releases are tag-driven (repo: github.com/TPAXTOP/e1001-power-dashboard; details in `docs/RELEASING.md`):

```powershell
python scripts/release.py X.Y.Z            # bumps version.h, dates CHANGELOG [Unreleased], commits + tags
git push origin main --follow-tags         # .github/workflows/release.yml builds, signs, publishes
python -m platformio run -e e1001 -t factory   # local merged image (scripts/factory_image.py)
```

Never hand-build or upload release assets: only the workflow has the signing key (`OTA_SIGNING_KEY` in the `release` environment). Tags `v*` and releases are immutable, so fix forward with a new version.

## Architecture

One-shot wake cycle, not a long-running app: `setup()` dispatches (first boot → provisioning AP portal; green button held → maintenance portal; otherwise → `wake_cycle::run()` → deep sleep). `loop()` is empty. Wakes follow one **grid**: the tick is the shortest effective interval of all enabled tasks (indoor sensor included), slots are epoch multiples of the tick (3 min → :00, :03, …), and `run()` sleeps until the next slot. Each wake runs every task due on its slot; nothing (no source, outage edge, night edge, OTA check) gets a wake of its own. WiFi is only switched on when a data source is due ("online wake"); everything else is an "offline wake".

| Path | Responsibility |
|---|---|
| `lib/dashcore/` | **Pure C++ (no Arduino)**: data structs (`data_model.h`) + display-derivation logic (`derive.cpp`) ported 1:1 from the web app's `lib/data-fetchers.ts` / `deye-api.ts`. Host-unit-tested. Behavior changes in `derive.cpp` must stay in parity with the web app semantics. `status.cpp` (device battery %, charge detection, rain chance, outage countdown, SOC bucket merge, hysteresis, connectivity), `schedule.cpp` (cadence, night window, due times, wake grid) and `alerts.cpp` (alert picking/formatting, ISO-8601 parse) are firmware-only and have no parity constraint. |
| `src/app/wake_cycle.cpp` | The state machine: battery policy → indoor sensor → RTC time → plan due tasks → (online only: WiFi, SNTP, due fetches, OTA check, NVS save, WiFi off) → build frame → `screen::present()` → next wake time |
| `src/app/maintenance.cpp` | Web portal (WebServer): edits ALL config in NVS, manual firmware upload. AP `EINK-SETUP-xxxx` on first boot / STA fallback |
| `src/net/` | `https.cpp` (one shared TLS client + embedded `certs/roots.pem`), `time_sync.cpp` (PCF8563 RTC ↔ system clock ↔ SNTP), `ota_pull.cpp` (signed version.json manifest → Update, rollback confirmation) |
| `src/api/` | One client per source; Yasno uses an ArduinoJson Filter to parse only the configured group from the all-groups response |
| `src/store/` | `config_store` (NVS ns "cfg" — every runtime setting), `state_store` (ns "state" + "cache": last-success epochs, Deye token, CRC-framed POD cache blobs), `rtc_state` (RTC RAM: last attempts, shown values for hysteresis, connectivity, WiFi BSSID/channel) |
| `src/hw/` | `sht4x.cpp`: onboard temperature/humidity sensor, minimal driver (no library), read at the start of each wake before WiFi warms the board |
| `src/ui/` | `display.cpp` (bare GxEPD2 driver + own 48KB `GFXcanvas1`; full refresh, or partial = push previous frame to OLD RAM, new frame, differential refresh), `screen.cpp` (Frame = all view structs; RTC-RAM snapshot of what is on the panel; skip/partial/full policy), `widgets.cpp` (tiles/graph/icons/dither), `render_power.cpp` (geometry port of `power.css`), `render_statusbar.cpp` (screen-wide bar on every page) |

Key invariants:
- **Stale-data UX**: every wake renders from the NVS-cached blobs; a source is "stale" ("!" badge) once its data is older than 2 × its effective interval + 60 s (`dash::isStale`). Cache must survive power loss → NVS, never RTC RAM.
- **Per-source intervals** (alerts 3 min, inverter status 3 min, SOC graph 15 min, outage 10 min, weather 30 min, indoor 3 min, fx 12 h — NVS-configurable). Effective interval: night window → at least `nightIntervalS`, low battery ×4, ≥3 WiFi failures ×2 (`dash::effectiveInterval`). A source is due after both its last success and its last attempt + interval, so failures retry once per interval.
- **Wake grid** (`dash::wakeTick` / `gridSlot` / `dueOnSlot`): a task runs on the slot nearest its due time (`dueAt <= slot + tick/2`), and success/attempt epochs are stamped with the slot, not "now", so due times never drift off the grid. Don't add separate event wakes; anything time-based must be handled on the next slot (outage edges at :00/:30 and the full hour are on a 3-min grid).
- **SOC graph is its own source** (`SRC_SOC`, blob "soc", `dash::SocHistory`): `historyRaw` is fetched incrementally (from the newest cached point − 30 min) and merged into 15-min buckets (`dash::mergeSocHistory`).
- **Offline wakes write no NVS** (flash wear at a wake every few minutes); their state lives in `rtc_state`. `state_store::save()` runs on online wakes and page switches only.
- **Screen = pure function of `screen::Frame`.** Renderers read only the view structs (and `localtime_r` of epochs in them), never the clock or globals, so the previous frame can be redrawn bit-exactly for a partial refresh. `screen::present()` skips the panel when the frame CRC is unchanged; otherwise full refresh on cold boot / manual refresh / unknown panel state / page change / `fullRefreshMin` elapsed (not at night) / `maxPartials` reached / night ended / indoor < 12 °C, else partial. Anything that changes every minute must be quantized (`dash::quantizeAge`, countdown steps, battery and indoor hysteresis) or it costs a refresh per wake. Bump `screen.cpp` `kMagic` when `Frame` changes layout; RTC RAM snapshots must be raw POD bytes (a static object with initializers is re-constructed on every boot).
- **Connectivity**: `dash::classifyConnectivity` from per-wake HTTPS counters (`net::counters()`): no WiFi / no internet (WiFi up, no HTTP response at all) / OK. "Inverter offline" = Deye answered with a reading that was already > 10 min old when fetched.
- **Times are Europe/Kyiv** via POSIX TZ `EET-2EEST,M3.5.0/3,M10.5.0/4`. Open-Meteo returns Kyiv-local strings — compare/slice them directly, no conversion (same as the web app).
- **Yasno groups are renumbered from time to time** (Kyiv: `1.1–6.2` became `1.1–60.1` in Oct 2026). The group is NVS config, never hardcoded. A missing group must render the "NO OUTAGE DATA" notice (`outageError` in `PowerView`): blank tiles would read as "no outages". A cache blob whose `groupId` differs from the config is never shown.
- **Layout departures from the web app** (firmware-only): the current-weather block (icon beside the temperature, rain chance over the next 3 h, indoor row with out-of-comfort values inverted) and the **status bar at y 450–480**. Pages must leave y ≥ 450 free. Status bar message priority: **fresh air raid alert** (Red = whole bar inverted, Yellow = dithered bar with white plates) > clock > no WiFi > no internet > inverter offline > alert API key rejected / no alert data yet > stale source (alerts first) > outage countdown > low battery. The connectivity icon and the device battery sit on the right (no render time: it would change every frame).
- **Alerts are only shown while fresh** (`!dash::isStale`): stale alert data shows neither an alert nor all-clear, but "Air alert data … old". Red beats Yellow, AIR beats other types (`dash::pickAlert`); a missing `activeAlertLevels` counts as Red; a response that is not an array is a failure (never a silent all-clear). Region ids are NVS config (default 31 = Kyiv city), max 3, one request each; a cache blob for other regions is never shown. Each check first asks `/api/v3/alerts/status` (as the API docs request) and refetches the regions only when `lastActionIndex` changed or the data is 15 min old.
- **Alert text is the API's Ukrainian `reason`** (fixed Ukrainian names when it is empty), drawn with the `_t_cyrillic` U8g2 fonts (10x20, then 9x15, then "..."), which have і/ї/є/ґ but no `·`, so alert texts use " з 14:05". Everything else in the status bar stays English with the helv fonts.
- **Device battery "since charge"**: there is no charger/USB detection, so `dash::updateCharge` infers charging from the pre-WiFi voltage: a new charge = reading ≥ 50 mV above the lowest since the last one (no "≥ X V means charged" shortcut); charging continues while each reading is within 15 mV of (or above) the session peak; the last such wake is `chargeEndEpoch` (NVS, saved immediately while charging). The status bar shows wall-clock `now − chargeEndEpoch`, or `charging`. Never go back to a fixed voltage threshold: a rested full cell stays above 4.15 V for hours after unplugging.
- Bump `dash::kCacheVersion` when any struct in `data_model.h` changes layout — old NVS blobs are then discarded instead of misread.
- Secrets never go in code or git; they live in NVS, entered via the portal. Deye password is stored as SHA256 only. The alert API key is never logged.
- **OTA trust chain**: the device installs a pulled image only if size + SHA-256 match `version.json` and its `sig` (ECDSA P-256 over `"<version>\n<sha256hex>\n"`) verifies against `certs/ota_signing_pub.pem` (embedded). Versions must be strictly newer by SemVer (`lib/dashcore/semver.cpp`). Manual portal uploads stay unsigned on purpose (physical button + LAN = recovery path).
- **Rollback**: `main.cpp` overrides `verifyRollbackLater()` → a new image boots PENDING_VERIFY. `ota_pull::confirmIfPending()` runs after an online render (wake_cycle) and on portal entry (maintenance + provisioning). Any reset before that rolls back, including deep sleep, so a pending image forces an online wake and `main.cpp` retries 3× before sleeping. Every new boot path that can deep-sleep or reboot must decide whether it confirms. `otaTriedVersion` → `otaBadVersion` (NVS state) stops retrying a rolled-back version. The periodic update check is time-based (`otaIntervalH`, `lastOtaCheckEpoch`) plus every cold boot.

## Hardware (include/pins.h)

EPD SPI: SCK 7, MOSI 9, CS 10, DC 11, RST 12, BUSY 13 (HSPI, `GxEPD2_750_GDEY075T7`). I2C 19/20: SHT4x 0x44, PCF8563 RTC 0x51 (charger chip/address unknown; see Gotchas). Buttons 3 (green) / 4 / 5, active-low, RTC-capable (ext1 ANY_LOW wake). LED 6 active-low, buzzer 45, battery ADC GPIO1 with 1:2 divider. **The divider is switched by GPIO21** (`BAT_EN_PIN`, high = on), so the ADC reads garbage unless it is enabled. `batteryVolts()` returns 0 ("unknown") outside 2.5–4.5 V so a bad read can never trigger "battery empty" sleep. USB-C goes through a CH340 USB-UART bridge, so `Serial` = UART0 and CDC-on-boot must stay off.

## Gotchas / current state

- **First flashed on real hardware 2026-10-06.** Confirmed: `qio_opi` boots, display/fonts, WiFi, Yasno, Deye latest, battery ADC (~4.0 V). Still unconfirmed: Deye SOC history parsing, USB detection. The TLS chains of all hosts were verified against `certs/roots.pem` with openssl in Oct 2026.
- Flash layout uses only 8MB (`partitions_8mb.csv`, `board_upload.flash_size = 8MB`), although the chip has 32MB. The prebuilt core is configured for ≤16MB quad flash with coredump-to-flash, and Seeed's guide selects 8MB. Don't place partitions above 16MB.
- The device can't be flashed over USB while it is in deep sleep. Press green first (or hold it for maintenance mode). The newcomer install flow lives in `INSTALL.md`; keep it in sync with portal/behavior changes.
- Platform is pinned (`pioarduino` 55.03.31 = Arduino core 3.3.1 / IDF 5.5). Don't float it; bump deliberately.
- Time: the RTC holds UTC, but other firmware (factory SenseCraft) leaves **local** time in it. Every cold boot forces SNTP, and `syncSntp()` waits for `sntp_get_sync_status() == COMPLETED`, never for "time looks valid" (the RTC time always does, which hid a 3 h offset on first flash).
- USB detection: there is **no charger at 0x6A** on real hardware, so `usbPresent()` is always false and "stay awake on USB" is inert. The cold-boot log line `I2C devices:` lists what actually responds, for finding the real charger.
- Deye returns numbers as JSON strings, not consistently, so parse via `numberFrom()` in deye_api.cpp, never `| 0` or `const char*` alone.
- Fonts: always call `display::setFont()`, never `u8g2().setFont()`. U8g2_for_Adafruit_GFX resets to solid-background mode on every font change, and its default background is 0 (black), so text renders as black boxes (seen on the first real flash).
- 1-bit display only: no grey — use the dither helpers in `widgets.cpp` (web CSS `#999` ≙ 50% checker).
- TLS: if a host rotates to an uncovered root CA (serial shows TLS errors), regenerate `certs/roots.pem` — see README "TLS root store" (scripts/make_roots.py + check_roots.ps1).
- otadata sits at 0x29000 in `partitions_8mb.csv`, so `board_upload.arduino.boot_app0 = 0x29000` is required. The core defaults to 0xe000, which is inside NVS, and a USB upload would then keep booting the old OTA slot. Keep the two in sync if the partition table changes.
- The OTA rollback path is unverified on hardware so far (as of 0.3.0). Test it with a deliberately crashing portal upload; see docs/RELEASING.md.
- Losing the signing key ends OTA for deployed devices; rotation is described in docs/RELEASING.md.
- Memory: one TLS connection at a time only; large JSON bodies go through `http.getString()` (>16KB allocs land in PSRAM automatically); never add concurrent fetches.
- HTTPS keep-alive: `net::httpGetJson/httpPostJson` share one `HTTPClient`, so consecutive requests to the same host reuse the TLS connection (a new `HTTPClient` per request always disconnects). Anything else using `net::tlsClient()` directly (the OTA download) must call `net::closeAll()` first.
- Partial refresh after deep sleep does not rely on the UC8179 keeping its RAM: the previous frame is redrawn from the RTC snapshot and written to OLD RAM (`writeImageToPrevious`) every time. Not yet verified on hardware (as of 0.4.0-rc.1): ghosting at the default 60 min / 30 partials, and SPI at 4 MHz.
- No PlatformIO registry access in the cloud sandbox: tests can be built with g++ + Unity directly; the firmware builds after providing `tool-scons` from PyPI and the libraries as git clones in `lib_extra_dirs`.
