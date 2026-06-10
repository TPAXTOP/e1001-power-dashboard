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
- Bump `dash::kCacheVersion` when any struct in `data_model.h` changes layout — old NVS blobs are then discarded instead of misread.
- Secrets never go in code or git; they live in NVS, entered via the portal. Deye password is stored as SHA256 only.

## Hardware (include/pins.h)

EPD SPI: SCK 7, MOSI 9, CS 10, DC 11, RST 12, BUSY 13 (HSPI, `GxEPD2_750_GDEY075T7`). I2C 19/20: SHT4x 0x44, PCF8563 RTC 0x51, SY6974B charger 0x6A. Buttons 3 (green) / 4 / 5, active-low, RTC-capable (ext1 ANY_LOW wake). LED 6 active-low, buzzer 45, battery ADC GPIO1 with 1:2 divider.

## Gotchas / current state

- **Never flashed to real hardware yet.** Untested risks: `board_build.arduino.memory_type = qio_opi` (boot-loop → try `opi_opi` or `qio_qspi`), Yasno CDN possibly blocking non-browser clients (browser-like UA is already set), Deye TLS chain.
- Platform is pinned (`pioarduino` 55.03.31 = Arduino core 3.3.1 / IDF 5.5). Don't float it; bump deliberately.
- 1-bit display only: no grey — use the dither helpers in `widgets.cpp` (web CSS `#999` ≙ 50% checker).
- TLS: if a host rotates to an uncovered root CA (serial shows TLS errors), regenerate `certs/roots.pem` — see README "TLS root store" (scripts/make_roots.py + check_roots.ps1).
- OTA has no bootloader rollback yet (prebuilt Arduino core): a boot-looping OTA image needs USB recovery. App-level `otaPendingVerify` flag exists; enabling real rollback via pioarduino `custom_sdkconfig` is a known future task.
- Memory: one TLS connection at a time only; large JSON bodies go through `http.getString()` (>16KB allocs land in PSRAM automatically); never add concurrent fetches.
