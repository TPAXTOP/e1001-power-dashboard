# E1001 Power Dashboard

[![CI](https://github.com/TPAXTOP/e1001-power-dashboard/actions/workflows/ci.yml/badge.svg)](https://github.com/TPAXTOP/e1001-power-dashboard/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/TPAXTOP/e1001-power-dashboard?sort=semver)](https://github.com/TPAXTOP/e1001-power-dashboard/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/ESP32--S3-PlatformIO%20%7C%20Arduino-orange)

Standalone ESP32-S3 firmware that turns a **Seeed Studio reTerminal E1001** (7.5" 800×480 monochrome e-paper) into
a battery-powered dashboard for life with scheduled blackouts in Kyiv. The device fetches all data itself over
HTTPS and renders it locally, with no server in between. It deep-sleeps between refreshes and runs for weeks on its
built-in battery.

<!-- Photo: add docs/images/device.jpg and uncomment
<p align="center"><img src="docs/images/device.jpg" width="640" alt="The dashboard on a reTerminal E1001"></p>
-->

## Features

- **Power outages** from the [Yasno](https://yasno.ua) planned-outage schedule for your group: today and tomorrow
  as 24 hour tiles, with half-hour precision and emergency-shutdown hatching. The status bar counts down to the
  next outage.
- **Backup power** from a Deye inverter through Deye Cloud: battery SOC, grid status, charge/discharge power with a
  runtime estimate, load, and a 24 h SOC graph.
- **Weather** from [Open-Meteo](https://open-meteo.com): current conditions, chance of rain over the next 3 h, and
  a 6-hour forecast. Indoor temperature and humidity come from the onboard sensor.
- **USD/UAH** 30-day chart on a second page (white buttons), via exchangerate.host.
- **Built for the battery:** each source has its own refresh cadence and WiFi only comes on when one is due. The
  screen is redrawn only when something on it changed, with a flicker-free partial refresh and a periodic full
  refresh against ghosting. Last-known-good data is cached in flash and shown with a "!" badge when fetches fail.
- **Connectivity at a glance:** a WiFi icon in the status bar, plus "No WiFi", "No internet" (WiFi up, nothing
  reachable) and "Inverter offline" (Deye cloud answers, but the logger stopped reporting) notices.
- **No reflash for settings:** WiFi, Yasno group, credentials, intervals and widgets are all set in a web portal on
  the device.
- **Safe automatic updates:** signed releases from this repo, hash and signature checked on the device, and an
  automatic rollback if a new version fails its first wake ([details](SECURITY.md#how-firmware-updates-are-protected)).

## Hardware

| | |
|---|---|
| Device | [Seeed Studio reTerminal E1001](https://www.seeedstudio.com/) (ESP32-S3, 8 MB PSRAM, 32 MB flash) |
| Display | 7.5" 800×480 monochrome e-paper, UC8179 (GxEPD2 `GDEY075T7`) |
| Sensors | SHT4x temperature/humidity, PCF8563 RTC, battery voltage ADC |
| Buttons | Green: refresh / hold for settings. White: switch page |

## Install

**Quick:** download `factory.bin` from the [latest release](https://github.com/TPAXTOP/e1001-power-dashboard/releases/latest)
and flash it with esptool. **From source:** PlatformIO. Both paths are covered step by step, including the USB
driver, waking the device for flashing, a factory backup, and the first-time setup portal, in
**[INSTALL.md](INSTALL.md)**.

Short version for when you already know the drill:

```powershell
pip install -r requirements-dev.txt
python -m platformio run -e e1001                 # build
python -m platformio run -e e1001 -t erase        # FIRST flash only (wipes factory data)
python -m platformio run -e e1001 -t upload       # press the green button first: no flashing while asleep
python -m platformio device monitor               # 115200 baud serial log
```

After the first flash, the screen shows **FIRST TIME SETUP**. Join the `EINK-SETUP-xxxx` WiFi it shows, open
`http://192.168.4.1`, and enter your WiFi (2.4 GHz), Yasno group, Deye and exchangerate.host credentials. Every
setting lives in NVS and survives firmware updates. To change settings later, **hold the green button ~1.5 s**. The
device then opens the same portal on your home WiFi (`http://eink.local` or the IP shown on screen).

## Updates

Devices update themselves. On cold boot and about every 12 h, the device reads
`releases/latest/download/version.json`. It installs a newer version only when the image's SHA-256 matches and the
ECDSA signature verifies against the key compiled into the firmware. The new image then has to draw the dashboard
while online before it is confirmed; otherwise the bootloader returns to the previous version. Updates are skipped
below 3.7 V.

You can also trigger an update from the portal (**Check for update now**), or upload any `firmware.bin` there by
hand.

Maintainers cut a release with two commands. CI builds, tests, signs and publishes it; see
[docs/RELEASING.md](docs/RELEASING.md):

```powershell
python scripts/release.py 0.3.1
git push origin main --follow-tags
```

## Buttons

| Button | Asleep | Awake (USB stay-awake mode) |
|---|---|---|
| Green (top), short press | fetch everything now + full refresh | — |
| Green (top), hold ~1.5 s | maintenance portal | — |
| White left/right | toggle power ⇄ FX page | toggle page |

## Power budget (2000 mAh battery)

WiFi plus TLS is what costs battery, not the wake itself or the panel. So the device wakes whenever a task is due
but switches WiFi on only for data sources: the inverter status every 3 min, the outage schedule every 10 min,
the battery graph every 15 min (fetching only the new part), weather every 30 min. Indoor readings, outage
start/end and the full hour are offline wakes that cost well under a second of CPU time. The panel is only
touched when the frame changed, mostly as a partial refresh (~0.5 s, no flashing).

Rough estimate with the defaults and the 03:00–08:00 night mode: 35–60 mAh per day, so about 4–7 weeks on the
2000 mAh battery (not yet measured; the status bar shows the real drain per day). Below 3.45 V the firmware
stretches every interval ×4. Below 3.30 V it shows "battery empty" and sleeps until a button press.

## Repository layout

| Path | Purpose |
|---|---|
| `lib/dashcore/` | Pure C++ data structs, derive logic and SemVer (host-unit-tested, no Arduino) |
| `src/app/` | Wake-cycle state machine, power management, maintenance portal |
| `src/net/` | WiFi, HTTPS + root store, SNTP/PCF8563 time, signed OTA pull |
| `src/api/` | Open-Meteo, Yasno, Deye, exchangerate.host clients |
| `src/store/` | NVS config and state/cache persistence |
| `src/ui/` | GxEPD2 display, widgets/icons, screen renderers |
| `include/pins.h` | E1001 pin map (EPD SPI 7/9/10/11/12/13, I2C 19/20, buttons 3/4/5) |
| `certs/` | Curated TLS root CAs and the OTA signing public key |
| `scripts/` | Release packaging/signing, factory image, root CA tooling |
| `test/` | Host unit tests (`python -m platformio test -e native`) |

## Development

```powershell
python -m platformio test -e native    # host unit tests (needs gcc/MinGW on PATH)
```

See [CONTRIBUTING.md](CONTRIBUTING.md). CI builds the firmware and runs the tests on every push and pull request.

### TLS root store

`certs/roots.pem` holds about 23 curated root CAs, verified against the actual chains of every host the firmware
contacts. If a host rotates to an uncovered CA (symptom: TLS errors in the serial log), regenerate it:

```powershell
Invoke-WebRequest https://curl.se/ca/cacert.pem -OutFile cacert.pem
python scripts/make_roots.py cacert.pem      # update WANTED list if needed
# scripts/check_roots.ps1 shows the current root CA of every host
```

## Acknowledgements

Data: [Open-Meteo](https://open-meteo.com) (CC BY 4.0), [Yasno](https://yasno.ua), Deye Cloud,
[exchangerate.host](https://exchangerate.host). Libraries: [GxEPD2](https://github.com/ZinggJM/GxEPD2),
[U8g2_for_Adafruit_GFX](https://github.com/olikraus/U8g2_for_Adafruit_GFX),
[ArduinoJson](https://arduinojson.org), [RTClib](https://github.com/adafruit/RTClib), and the
[pioarduino](https://github.com/pioarduino/platform-espressif32) ESP32 platform.

This is an independent hobby project, not affiliated with Seeed Studio, Yasno, Deye or any data provider.

## License

[MIT](LICENSE)
