# eink-firmware — reTerminal E1001 standalone dashboard

Native ESP32-S3 firmware for the **Seeed Studio reTerminal E1001** (7.5" 800x480
monochrome ePaper) that replaces the SenseCraft-HMI + Vercel web dashboard chain.
The device fetches all data itself over HTTPS and renders the dashboard locally:

- **Weather** — Open-Meteo (Kyiv): current conditions + 6-hour forecast
- **Power outages** — Yasno planned-outage schedule (today/tomorrow, 24 hour
  tiles with half-hour diagonals and EmergencyShutdowns zebra pattern)
- **Backup power** — Deye Cloud inverter: battery SOC, grid status,
  charge/discharge + runtime estimate, load, 24h SOC graph
- **USD/UAH** — exchangerate.host 30-day chart (second page, white buttons)

Battery-first: the device deep-sleeps between wakes (default 10 min, configurable
down to ~5 min) and does one full e-paper refresh per wake. Failed refreshes fall
back to the last-known-good data cached in NVS, marked with a "!" badge.

## Repository layout

| Path | Purpose |
|---|---|
| `lib/dashcore/` | Pure data structs + derive logic (host-unit-tested, no Arduino) |
| `src/app/` | Wake-cycle state machine, power management, maintenance portal |
| `src/net/` | WiFi, HTTPS+root store, SNTP/PCF8563 time, OTA pull |
| `src/api/` | Open-Meteo / Yasno / Deye / exchangerate.host clients |
| `src/store/` | NVS config + state/cache persistence |
| `src/ui/` | GxEPD2 display, widgets/icons, screen renderers |
| `include/pins.h` | E1001 pin map (EPD SPI 7/9/10/11/12/13, I2C 19/20, buttons 3/4/5) |
| `certs/roots.pem` | Curated root CAs embedded for TLS (see scripts/make_roots.py) |
| `test/test_derive/` | Host unit tests (`python -m platformio test -e native`) |

## Install

**New to this? Follow [INSTALL.md](INSTALL.md)**. It is a step-by-step guide covering the USB driver, waking the
device for flashing, a factory backup, the first flash, the setup portal (with a field-by-field mapping from the web
dashboard's `.env.local`), and troubleshooting.

Short version, for when you already know the drill (PlatformIO is not on PATH here, so use `python -m platformio`):

```powershell
python -m platformio run -e e1001                 # build
python -m platformio run -e e1001 -t erase        # FIRST flash only (wipes factory data)
python -m platformio run -e e1001 -t upload       # press the green button first: no flashing while asleep
python -m platformio device monitor               # 115200 baud serial log
```

After the first flash, the screen shows **FIRST TIME SETUP**. Join the `EINK-SETUP-xxxx` WiFi it shows, open
`http://192.168.4.1`, and enter your WiFi (2.4 GHz), Yasno group, Deye and exchangerate.host credentials. Every
setting lives in NVS and survives firmware updates. To change settings later, **hold the green button ~1.5 s**. The
device then wakes into **maintenance mode** and serves the same form on your home WiFi (`http://eink.local` or the IP
shown on screen). If home WiFi is unreachable, it opens its own access point and shows its credentials on screen.

## Updating over WiFi

Two mechanisms, no USB cable required:

- **Manual:** maintenance mode → *Firmware update* → upload `.pio/build/e1001/firmware.bin`.
- **Automatic (pull OTA):** publish a release and set its manifest URL in the
  portal. The device checks every ~12 h and updates itself:

  ```bash
  python -m platformio run -e e1001
  python scripts/gen_version.py https://github.com/<user>/<repo>/releases/download/v0.2.0
  # upload release/firmware.bin + release/version.json to the GitHub release,
  # point "OTA manifest URL" at .../releases/latest/download/version.json
  ```

  Bump `APP_VERSION` in `include/version.h` for every release — the device
  only installs strictly newer semver. Config/NVS survives updates; most
  behavior tweaks (intervals, group, widgets, credentials) need **no reflash**
  at all, just the portal.

## Buttons

| Button | Asleep | Awake (USB stay-awake mode) |
|---|---|---|
| Green (top), short press | refresh now | — |
| Green (top), hold ~1.5 s | maintenance portal | — |
| White left/right | toggle power ⇄ FX page | toggle page |

## Tests

```powershell
$env:PATH = "$env:LOCALAPPDATA\mingw-portable\mingw64\bin;$env:PATH"   # host gcc
python -m platformio test -e native    # derive-logic unit tests
```

## TLS root store

`certs/roots.pem` holds ~23 curated root CAs (verified 2026-06 against the
actual chains of every host the firmware contacts). If a host rotates to an
uncovered CA (symptom: TLS errors in the serial log), regenerate:

```powershell
Invoke-WebRequest https://curl.se/ca/cacert.pem -OutFile cacert.pem
python scripts/make_roots.py cacert.pem      # update WANTED list if needed
# scripts/check_roots.ps1 shows the current root CA of every host
```

## Power budget (2000 mAh battery)

Roughly 15–25 s awake per cycle (WiFi + 4 TLS fetches + 4–5 s EPD refresh):
≈ 10–12 days at a 5-min interval, ≈ 3 weeks at 10 min. Below 3.45 V the
firmware stretches the interval ×4; below 3.30 V it shows "battery empty" and
sleeps until a button press. On USB power, enable *Stay awake on USB* for
instant page switching and an always-listening portal.
