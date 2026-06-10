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
| `test/test_derive/` | Host unit tests (`pio test -e native`) |

## Building & first flash (USB)

Requires [PlatformIO](https://platformio.org) (`pip install platformio`).

```bash
pio run -e e1001                 # build
pio run -e e1001 -t upload       # first flash over USB-C
pio device monitor               # 115200 baud serial log
```

> If the device boot-loops immediately after flashing, the PSRAM/flash mode
> guess is wrong: change `board_build.arduino.memory_type` in `platformio.ini`
> (`qio_opi` -> `opi_opi` or `qio_qspi`) and rebuild.

## First-time setup (no secrets in the repo)

1. Flash and power on. With no WiFi credentials stored, the screen shows
   **FIRST TIME SETUP** with an access-point name/password.
2. Join the `EINK-SETUP-xxxx` WiFi from a phone, open `http://192.168.4.1`.
3. Enter WiFi credentials, Yasno group (1.1–6.2), Deye developer credentials
   (App ID/Secret, email, password — stored only as SHA256, device SN,
   battery Wh), exchangerate.host key, intervals. **Save & Reboot.**

All of these are stored in NVS and survive firmware updates. To change them
later: **hold the green (top) button ~1.5 s** while the device is asleep — it
wakes into **maintenance mode** and serves the same form on your home WiFi
(`http://eink.local` or the IP shown on screen).

## Updating over WiFi

Two mechanisms, no USB cable required:

- **Manual:** maintenance mode → *Firmware update* → upload `firmware.bin`.
- **Automatic (pull OTA):** publish a release and set its manifest URL in the
  portal. The device checks every ~12 h and updates itself:

  ```bash
  pio run -e e1001
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

```bash
pio test -e native    # derive-logic unit tests (needs host gcc/g++)
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
