# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Standalone ESP32-S3 firmware for the **Seeed Studio reTerminal E1001** (7.5" 800x480 monochrome ePaper, UC8179 controller). Recreates the `/power` page of the Next.js dashboard at `D:\WebProjects\eink-web-dashboard` (kept as the behavior/layout reference — do not modify it from here). The device fetches all data itself over HTTPS and deep-sleeps between refreshes.

Data sources: Open-Meteo (Kyiv weather), Yasno (power outage schedule, group-filtered), Deye Cloud (inverter/battery, token auth), exchangerate.host (USD/UAH, second page).

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

One-shot wake cycle, not a long-running app: `setup()` dispatches (first boot → provisioning AP portal; green button held → maintenance portal; otherwise → `wake_cycle::run()` → deep sleep). `loop()` is empty.

| Path | Responsibility |
|---|---|
| `lib/dashcore/` | **Pure C++ (no Arduino)**: data structs (`data_model.h`) + display-derivation logic (`derive.cpp`) ported 1:1 from the web app's `lib/data-fetchers.ts` / `deye-api.ts`. Host-unit-tested. Behavior changes in `derive.cpp` must stay in parity with the web app semantics. `status.cpp` is firmware-only (device battery %, drain rate, rain chance, outage countdown) and has no parity constraint. |
| `src/app/wake_cycle.cpp` | The state machine: battery policy → RTC/SNTP time → WiFi → per-source fetch-or-cache → render → OTA check → sleep duration |
| `src/app/maintenance.cpp` | Web portal (WebServer): edits ALL config in NVS, manual firmware upload. AP `EINK-SETUP-xxxx` on first boot / STA fallback |
| `src/net/` | `https.cpp` (one shared TLS client + embedded `certs/roots.pem`), `time_sync.cpp` (PCF8563 RTC ↔ system clock ↔ SNTP), `ota_pull.cpp` (signed version.json manifest → Update, rollback confirmation) |
| `src/api/` | One client per source; Yasno uses an ArduinoJson Filter to parse only the configured group from the all-groups response |
| `src/store/` | `config_store` (NVS ns "cfg" — every runtime setting), `state_store` (ns "state" + "cache": last-success epochs, Deye token, CRC-framed POD cache blobs) |
| `src/hw/` | `sht4x.cpp`: onboard temperature/humidity sensor, minimal driver (no library), read at the start of each wake before WiFi warms the board |
| `src/ui/` | `display.cpp` (GxEPD2, full-height 48KB buffer, one full refresh per wake), `widgets.cpp` (tiles/graph/icons/dither), `render_power.cpp` (geometry port of `power.css`), `render_statusbar.cpp` (screen-wide bar on every page) |

Key invariants:
- **Stale-data UX**: fetch failure → render NVS-cached blob + "!" badge (`acquire()` in wake_cycle.cpp). Cache must survive power loss → NVS, never RTC RAM.
- **Per-source max-age** decouples fetch cadence from wake cadence (outage/backup 5 min, weather 30 min, fx 12 h — all NVS-configurable).
- **Times are Europe/Kyiv** via POSIX TZ `EET-2EEST,M3.5.0/3,M10.5.0/4`. Open-Meteo returns Kyiv-local strings — compare/slice them directly, no conversion (same as the web app).
- **Yasno groups are renumbered from time to time** (Kyiv: `1.1–6.2` became `1.1–60.1` in Oct 2026). The group is NVS config, never hardcoded. A missing group must render the "NO OUTAGE DATA" notice (`outageError` in `PowerView`): blank tiles would read as "no outages". A cache blob whose `groupId` differs from the config is never shown.
- **Layout departures from the web app** (firmware-only): the current-weather block (icon beside the temperature, rain chance over the next 3 h, indoor row with out-of-comfort values inverted) and the **status bar at y 450–480**. Pages must leave y ≥ 450 free. Status bar message priority: clock > WiFi > stale source > outage countdown > low battery. Render time and the device battery sit on the right.
- **Device battery "since full"**: there is no charger/USB detection, so every wake with `vbat >= vbatFull` (NVS, default 4.15 V) stores `lastFullEpoch`. The timer therefore counts from when the device came off the charger. Drain rate = (100 − %) / days since full, shown after 12 h.
- Bump `dash::kCacheVersion` when any struct in `data_model.h` changes layout — old NVS blobs are then discarded instead of misread.
- Secrets never go in code or git; they live in NVS, entered via the portal. Deye password is stored as SHA256 only.
- **OTA trust chain**: the device installs a pulled image only if size + SHA-256 match `version.json` and its `sig` (ECDSA P-256 over `"<version>\n<sha256hex>\n"`) verifies against `certs/ota_signing_pub.pem` (embedded). Versions must be strictly newer by SemVer (`lib/dashcore/semver.cpp`). Manual portal uploads stay unsigned on purpose (physical button + LAN = recovery path).
- **Rollback**: `main.cpp` overrides `verifyRollbackLater()` → a new image boots PENDING_VERIFY. `ota_pull::confirmIfPending()` runs after an online render (wake_cycle) and on portal entry (maintenance + provisioning). Any reset before that rolls back, including deep sleep, so `main.cpp` retries offline wakes 3× before sleeping. Every new boot path that can deep-sleep or reboot must decide whether it confirms. `otaTriedVersion` → `otaBadVersion` (NVS state) stops retrying a rolled-back version.

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
