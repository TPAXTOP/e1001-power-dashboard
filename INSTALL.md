# Installing on the reTerminal E1001 — step by step

This guide assumes you are new to ESP32 work. Commands are for **Windows PowerShell**, run from the repo root
(the folder you cloned, e.g. `git clone https://github.com/TPAXTOP/e1001-power-dashboard`). The first install takes
about 30 minutes, most of it downloads.

Do steps 1–6 once, over USB. After that, every change goes over WiFi: settings, the Yasno group, and firmware
updates, which the device installs by itself from this repository's
[releases](https://github.com/TPAXTOP/e1001-power-dashboard/releases).

> **No build environment wanted?** Every release has a ready-made `factory.bin`. See
> [Flash a prebuilt release](#flash-a-prebuilt-release-instead-of-building) and skip steps 2 and 6.

---

## 1. Prerequisites

| What | How |
|---|---|
| Python 3 | Already installed if `python --version` works |
| PlatformIO (the build tool) | `pip install platformio`. It is **not** on PATH, so always call it as `python -m platformio …` |
| USB-C **data** cable | Charge-only cables are common and fail silently. If no COM port shows up, try another cable |
| USB driver | The E1001 has a CH340 USB-serial chip. Windows 11 usually installs the driver automatically. Check Device Manager → *Ports (COM & LPT)* for "USB-SERIAL CH340 (COMx)". If it is missing, install the WCH CH341SER driver |
| Device power switch | Set it to **ON**. The device can't be flashed while it is switched off |

What PlatformIO does: it downloads the ESP32 compiler and libraries, builds `firmware.bin`, and writes it to the
device's flash chip over the USB-serial link. It picks the COM port automatically.

## 2. Build

```powershell
python -m platformio run -e e1001
```

The first run downloads about 1 GB of toolchain and takes several minutes. It should end with `[SUCCESS]`. The output
is `.pio\build\e1001\firmware.bin`.

### Flash a prebuilt release instead of building

1. Install only the flashing tool: `pip install esptool`. You still need the USB driver and cable from step 1.
2. Download `factory.bin` from the [latest release](https://github.com/TPAXTOP/e1001-power-dashboard/releases/latest).
   Optionally, check it against `SHA256SUMS` from the same release with `Get-FileHash factory.bin`.
3. Do steps 3–5 below, using `python -m esptool` wherever the guide says `& $esptool`.
4. Instead of step 6, press green and run:

   ```powershell
   python -m esptool erase-flash                  # FIRST install only, wipes factory data
   python -m esptool write-flash 0x0 factory.bin  # press green first
   ```

5. Continue with step 7. To watch the log without PlatformIO, use any serial terminal at 115200 baud.

## 3. Wake the device before any USB step

The device spends most of its time in deep sleep, and **you cannot flash it while it is asleep**. Before each USB
command below, **press the green button** on top of the device and start the command within a few seconds.

> Once our firmware is running, there's a more relaxed option: **hold the green button for about 1.5 s** to enter
> maintenance mode. The device then stays awake for 10 minutes.

## 4. Check the connection and flash chip

PlatformIO ships its own `esptool` (the ESP flashing utility):

```powershell
$esptool = "$env:USERPROFILE\.platformio\penv\Scripts\esptool.exe"
& $esptool flash-id
```

Expect `Chip type: ESP32-S3`, `Detected flash size: 32MB`, and a PSRAM line. If it says *"No serial data
received"* or *"Failed to connect"*, the device was asleep (press green and retry), or the cable or driver is the
problem (see step 1).

## 5. (Recommended) Back up the factory firmware

This step is the only way back to stock SenseCraft without Seeed's web flasher. It takes about 10 minutes and must
run while the device is awake, so press green first:

```powershell
& $esptool read-flash 0 ALL factory-backup.bin
```

Keep `factory-backup.bin` outside the repo. `*.bin` is git-ignored anyway. To restore it later, run
`& $esptool write-flash 0 factory-backup.bin`.

## 6. Erase once, flash, and watch the log

```powershell
python -m platformio run -e e1001 -t erase     # wipes leftover factory settings - press green first
python -m platformio run -e e1001 -t upload    # press green first
python -m platformio device monitor            # live log, Ctrl+C to quit
```

The erase is needed only on this very first flash. It clears the factory firmware's stored data, so our firmware
starts from a clean state. Never erase again, because that would wipe your saved settings.

After the upload, the screen shows **FIRST TIME SETUP**, and the log ends with
`I maint portal at http://192.168.4.1 (EINK-SETUP-xxxx)`. Some red `[E]` lines on this boot are harmless:

- `Preferences.cpp … nvs_open failed: NOT_FOUND`: no settings are saved yet. You may also see it later for empty caches.
- `__digitalWrite(): IO 10/11/12 is not set as GPIO`: the display driver sets its pins high just before configuring
  them. This is a known cosmetic warning with this Arduino core.

> **If the log shows a boot loop** (it repeats from `ESP-ROM:` endlessly), the memory mode guess is wrong. In
> `platformio.ini`, change `board_build.arduino.memory_type = qio_opi` to `opi_opi` and run upload again.

## 7. First-time setup (WiFi + credentials)

1. Note the network name and password shown on the screen. They look like `EINK-SETUP-1A2B` / `eink…`.
2. Join that WiFi from your phone or laptop. It has no internet, which is expected. Open **http://192.168.4.1**.
3. Fill in the form. All values except WiFi and the Yasno group are already in
   `..\eink-web-dashboard\.env.local`:

   | Portal field | Value |
   |---|---|
   | WiFi SSID / Password | Your home WiFi. **It must be 2.4 GHz**, because the ESP32 can't see 5 GHz-only networks |
   | Refresh intervals | Defaults are fine: air raid alerts 180 s, inverter status 180 s, battery graph 900 s, outage schedule 600 s, weather 1800 s, indoor sensor 180 s. The device wakes at the shortest of them and refreshes everything due on that wake; WiFi is only switched on when a source is due |
   | Night mode | On, 03:00–08:00 by default: everything refreshes at most every 15 min. Untick it if you want full speed around the clock |
   | Screen | A full refresh (the black/white flash that clears ghosting) every `360` min, and optionally after N partial refreshes (`0` = no limit). Lower them if ghosting builds up |
   | Air raid alerts: API key | Your api.ukrainealarm.com key (request one on that site) |
   | Air raid alerts: Region ids | `31` (Kyiv city). Up to 3, comma-separated; ids are listed at `/api/v3/regions` |
   | Yasno group | Your **current** group, e.g. `29.1`. Yasno renumbered Kyiv groups (the old `3.2` no longer exists). Check your address on yasno.ua |
   | Deye App ID | `DEYE_APP_ID` |
   | Deye App Secret | `DEYE_APP_SECRET` |
   | Account email | `DEYE_EMAIL` |
   | Account password | `DEYE_PASSWORD` (plain; the device stores only its SHA256) — **or** leave it empty and paste `DEYE_PASSWORD_HASHED` into "SHA256 hex directly" |
   | Device serial number | `DEYE_DEVICE_SN` |
   | Battery capacity (Wh) | `5120` unless your battery differs (used for the runtime estimate) |
   | exchangerate.host API key | `EXCHANGERATE_API_KEY` |
   | Update manifest URL | Leave empty. Empty means this project's latest signed release |
   | Check for updates every N hours | `12`. `0` checks only at power-on |
   | Indoor climate | Defaults are fine. If the indoor temperature reads high or low, set an offset later; the line at the top of the portal shows raw sensor and battery readings |

4. Click **Save & Reboot**. The device joins your WiFi, fetches everything, and draws the dashboard within about
   30 s.

### What a healthy first run looks like in `device monitor`

```
I main     eink-dash 0.3.0
I power    I2C devices: 0x44 0x51 ...
I cycle    boot=1 vbat=3.95V usb=0
I wifi     connected, ip=192.168.x.x rssi=-55 (2100 ms)
I time     SNTP synced: 2026-10-06 23:40 Kyiv (clock was off by ...s)
I weather  ok: 12.3C code=3 hourly=8
I yasno    ok: today[2026-10-06 ScheduleApplies 3 slots] tomorrow[...]
I deye     authenticated, token valid ...s
I deye     history: 288 raw -> 96 points
I deye     ok: soc=87% grid=1 battW=... loadW=... status=...
I ota      up to date (0.3.0, latest 0.3.0)
I power    deep sleep for ...s
```

On screen, the outage widget header reads **POWER OUTAGE … GROUP 29.1**. Today's outage hours are drawn as black
tiles. The bar along the bottom shows the next outage (or a problem, marked with "!") on the left. An air raid
alert replaces it, in Ukrainian as the alert service words it: a red alert (`ПОВІТРЯНА ТРИВОГА з 14:05`) turns
the whole bar black, a yellow one (`Дронова загроза (жовтий рівень) з 13:40`) makes it hatched grey. The right side shows connectivity and the device battery: `80% · 3d 4h` means 80% charge and 3 days 4
hours since the last charge ended (`80% · charging` while it charges).

## 8. Everyday use

| Button | While asleep (normal) |
|---|---|
| Green (top), short press | Refresh now |
| Green (top), **hold ~1.5 s** | Maintenance mode: settings portal + firmware upload |
| White left / right | Switch between the power page and the USD/UAH page |

**To change settings later** (group, interval, credentials): hold green. The screen then shows the portal address,
`http://eink.local` or an IP. Open it on a device that is on the same WiFi. No reflashing is needed.

**Firmware updates are automatic.** On every cold boot and then about every 12 h, the device checks this
repository's latest release. It installs the release only if the image is signed with the project key and its hash
matches. A new version gets one chance to prove itself: if it can't draw the dashboard while online, the device
rolls back to the previous version by itself and won't retry that one. Updates are skipped while the battery is
below 3.7 V. Your settings survive updates.

- **Update right now:** hold green, open the portal, and click **Check for update now**.
- **Install your own build** (unsigned is fine here, because you are physically at the device): build with
  `python -m platformio run -e e1001`, hold green, choose **Firmware update**, select
  `.pio\build\e1001\firmware.bin`, and click **Upload & Flash**. An automatic update later replaces it with the
  newest release.

> **Upgrading a device that runs 0.2.0 or older:** those versions can't check signatures, and their update URL is
> empty. Install 0.3.0 once by hand: either the USB upload from step 6 (no erase), or the portal upload of
> `firmware.bin` from the [v0.3.0 release](https://github.com/TPAXTOP/e1001-power-dashboard/releases/tag/v0.3.0).
> Every later version arrives by itself.

## 9. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Outage widget says **NO OUTAGE DATA – Group X not found** | Yasno doesn't know that group (it renumbers occasionally). Hold green and fix the group |
| Outage widget says **No WiFi connection** | WiFi is down or the password is wrong. Hold green. If home WiFi is unreachable, the portal opens its own `EINK-SETUP-…` network, and its name and password are shown on screen |
| A black **"!"** next to a widget title | That source failed this time. The last good data is shown. Check `device monitor` for the reason |
| TLS / certificate errors in the log | A server switched to a root CA we don't embed. See "TLS root store" in README.md |
| `https GET 403 …yasno…` | Yasno's CDN blocked the request. Report this along with the log |
| Screen says **BATTERY EMPTY** | Connect USB-C, then press any button |
| No indoor row under the weather | The onboard sensor didn't answer. Check that `0x44` is in the `I2C devices:` log line |
| Battery shows no "since charge" time | No charge seen since the update. Charge the device once; the status bar shows `charging` while it does. The `charge:` log line shows what the detection sees |
| Status bar says **Air alert API key rejected** | The api.ukrainealarm.com key was refused 5 times in a row. The API also refuses a key that is used more than about once a minute, so make sure no other app or script uses the same key. Otherwise the key is wrong or revoked: hold green and fix it |
| Upload fails with "Failed to connect" | The device was asleep. Press green and retry immediately |
| Boot loop right after flashing | See the note in step 6 (`memory_type`) |
| Portal says an update **was rolled back** | That version failed its first wake and the device went back. The device skips it until a newer release appears. **Check for update now** retries it on purpose. Please open an issue with the serial log |
| `ota … manifest signature invalid` | The release wasn't signed with the project key. The device refuses it on purpose |
