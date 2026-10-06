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

Release for pull-OTA (after bumping `APP_VERSION` in `include/version.h`):

```powershell
python scripts/gen_version.py https://github.com/<user>/<repo>/releases/download/v<X.Y.Z>
# upload release/firmware.bin + release/version.json as release assets
```

## Architecture

One-shot wake cycle, not a long-running app: `setup()` dispatches (first boot → provisioning AP portal; green button held → maintenance portal; otherwise → `wake_cycle::run()` → deep sleep). `loop()` is empty.

| Path | Responsibility |
|---|---|
| `lib/dashcore/` | **Pure C++ (no Arduino)**: data structs (`data_model.h`) + display-derivation logic (`derive.cpp`) ported 1:1 from the web app's `lib/data-fetchers.ts` / `deye-api.ts`. Host-unit-tested. Behavior changes here must stay in parity with the web app semantics. |
| `src/app/wake_cycle.cpp` | The state machine: battery policy → RTC/SNTP time → WiFi → per-source fetch-or-cache → render → OTA check → sleep duration |
| `src/app/maintenance.cpp` | Web portal (WebServer): edits ALL config in NVS, manual firmware upload. AP `EINK-SETUP-xxxx` on first boot / STA fallback |
| `src/net/` | `https.cpp` (one shared TLS client + embedded `certs/roots.pem`), `time_sync.cpp` (PCF8563 RTC ↔ system clock ↔ SNTP), `ota_pull.cpp` (version.json manifest → Update) |
| `src/api/` | One client per source; Yasno uses an ArduinoJson Filter to parse only the configured group from the all-groups response |
| `src/store/` | `config_store` (NVS ns "cfg" — every runtime setting), `state_store` (ns "state" + "cache": last-success epochs, Deye token, CRC-framed POD cache blobs) |
| `src/ui/` | `display.cpp` (GxEPD2, full-height 48KB buffer, one full refresh per wake), `widgets.cpp` (tiles/graph/icons/dither), `render_power.cpp` (geometry port of `power.css`) |

Key invariants:
- **Stale-data UX**: fetch failure → render NVS-cached blob + "!" badge (`acquire()` in wake_cycle.cpp). Cache must survive power loss → NVS, never RTC RAM.
- **Per-source max-age** decouples fetch cadence from wake cadence (outage/backup 5 min, weather 30 min, fx 12 h — all NVS-configurable).
- **Times are Europe/Kyiv** via POSIX TZ `EET-2EEST,M3.5.0/3,M10.5.0/4`. Open-Meteo returns Kyiv-local strings — compare/slice them directly, no conversion (same as the web app).
- **Yasno groups are renumbered from time to time** (Kyiv: `1.1–6.2` became `1.1–60.1` in Oct 2026). The group is NVS config, never hardcoded. A missing group must render the "NO OUTAGE DATA" notice (`outageError` in `PowerView`): blank tiles would read as "no outages". A cache blob whose `groupId` differs from the config is never shown.
- Bump `dash::kCacheVersion` when any struct in `data_model.h` changes layout — old NVS blobs are then discarded instead of misread.
- Secrets never go in code or git; they live in NVS, entered via the portal. Deye password is stored as SHA256 only.

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
- OTA has no effective rollback yet: a boot-looping OTA image needs USB recovery. The prebuilt core *does* have `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, but Arduino's weak `verifyRollbackLater()` returns false, so every image is marked valid at boot. Future task: override it and mark the image valid on every boot path (wake cycle, maintenance, provisioning), not only after a render, otherwise a deep-sleep or portal reboot would roll back a good image. The app-level `otaPendingVerify` flag exists.
- Memory: one TLS connection at a time only; large JSON bodies go through `http.getString()` (>16KB allocs land in PSRAM automatically); never add concurrent fetches.
