# Installing on the reTerminal E1001 — step by step

This guide assumes you are new to ESP32 work. Commands are for **Windows PowerShell**, run from the repo root
(`D:\WebProjects\eink-firmware`). The first install takes about 30 minutes, most of it downloads.

Do steps 1–6 once, over USB. After that, every change (settings, Yasno group, firmware updates) goes over WiFi.

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
   | Wake interval | `600` (10 min) is the default. Use `300` while outages are active, which gives about 10 days on battery instead of about 3 weeks |
   | Yasno group | Your **current** group, e.g. `29.1`. Yasno renumbered Kyiv groups (the old `3.2` no longer exists). Check your address on yasno.ua |
   | Deye App ID | `DEYE_APP_ID` |
   | Deye App Secret | `DEYE_APP_SECRET` |
   | Account email | `DEYE_EMAIL` |
   | Account password | `DEYE_PASSWORD` (plain; the device stores only its SHA256) — **or** leave it empty and paste `DEYE_PASSWORD_HASHED` into "SHA256 hex directly" |
   | Device serial number | `DEYE_DEVICE_SN` |
   | Battery capacity (Wh) | `5120` unless your battery differs (used for the runtime estimate) |
   | exchangerate.host API key | `EXCHANGERATE_API_KEY` |
   | OTA manifest URL | Leave empty |
   | Indoor climate, Device battery | Defaults are fine. If the indoor temperature reads high or low, set an offset later; the line at the top of the portal shows raw sensor and battery readings |

4. Click **Save & Reboot**. The device joins your WiFi, fetches everything, and draws the dashboard within about
   30 s.

### What a healthy first run looks like in `device monitor`

```
I main     eink-dash 0.1.2
I power    I2C devices: 0x44 0x51 ...
I cycle    boot=1 vbat=3.95V usb=0
I wifi     connected, ip=192.168.x.x rssi=-55 (2100 ms)
I time     SNTP synced: 2026-10-06 23:40 Kyiv (clock was off by ...s)
I weather  ok: 12.3C code=3 hourly=8
I yasno    ok: today[2026-10-06 ScheduleApplies 3 slots] tomorrow[...]
I deye     authenticated, token valid ...s
I deye     history: 288 raw -> 96 points
I deye     ok: soc=87% grid=1 battW=... loadW=... status=...
I power    deep sleep for ...s
```

On screen, the outage widget header reads **POWER OUTAGE … GROUP 29.1**. Today's outage hours are drawn as black
tiles. The bar along the bottom shows the next outage (or a problem, marked with "!") on the left. The right side
shows the refresh time and the device battery: `80% · 3d 4h · 6%/d` means 80% charge, 3 days 4 hours since the
last full charge, and an average drain of 6% per day.

## 8. Everyday use

| Button | While asleep (normal) |
|---|---|
| Green (top), short press | Refresh now |
| Green (top), **hold ~1.5 s** | Maintenance mode: settings portal + firmware upload |
| White left / right | Switch between the power page and the USD/UAH page |

**To change settings later** (group, interval, credentials): hold green. The screen then shows the portal address,
`http://eink.local` or an IP. Open it on a device that is on the same WiFi. No reflashing is needed.

**To update the firmware later, without a cable:**
1. Build with `python -m platformio run -e e1001`.
2. Hold green, open the portal, choose **Firmware update**, and select `.pio\build\e1001\firmware.bin`.
3. Click **Upload & Flash**.

Your settings survive updates.

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
| Battery widget never shows the "since full" time | The ADC never reads the full-charge voltage. Charge to full, open the portal, and set **Full-charge voltage** about 0.03 V below the battery voltage shown at the top |
| Upload fails with "Failed to connect" | The device was asleep. Press green and retry immediately |
| Boot loop right after flashing | See the note in step 6 (`memory_type`) |
