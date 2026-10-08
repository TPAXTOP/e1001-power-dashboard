# Changelog

All notable changes to this firmware are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). Devices update themselves to the
newest published (non-pre)release; see [docs/RELEASING.md](docs/RELEASING.md).

## [Unreleased]

### Added
- Diagnostic log on a microSD card (optional). Log lines are kept in RAM during a wake and written to the card right
  before deep sleep, so the card is only powered for a fraction of a second per wake. `/log/YYYY-MM-DD.log` has
  every log line with its local time. Each wake starts with the firmware version, the wake reason and the reset
  reason (crashes and brownouts show up there). `/log/wakes-YYYY-MM.csv` has one row per wake: battery voltage,
  WiFi join and on time, requests, the air alert HTTP status, the screen refresh, the awake time and the sleep
  that followed. The oldest day files are deleted when the card is 85 % full. The portal shows whether a card is
  in, and lists the files to view or download (**Log files**).
- An SD card icon left of the WiFi icon while a card is in the slot; slashed when the last write failed.
- Every HTTPS request is logged with its status and duration. For error responses, the rate-limit-related headers
  (`Date`, `Retry-After`, `X-*`, `*RateLimit*`, ...) and the start of the body are logged too. The air alert
  request also logs the seconds since the previous one and the number of refusals since the last success. Together
  these show whether the API's 401s are its per-minute rate limit or something else.
- The "battery empty" screen shows how long the device ran since the last charge and the firmware version.
- Air raid alerts from api.ukrainealarm.com in the status bar, above every other message, in Ukrainian with the
  API's own wording (e.g. `Дронова загроза (жовтий рівень) з 13:40`). Red alerts, which usually come without a
  text, get the official names (`ПОВІТРЯНА ТРИВОГА з 14:05`, `ЗАГРОЗА АРТОБСТРІЛУ`, ...). A red alert turns the
  whole bar black; a yellow one is a grey hatched bar with the text on a white plate. `+N` counts further active
  alert types. Checked every 3 min (15 min at night) with exactly one request: the API refuses (HTTP 401) a key
  that is used again within about a minute, so the key must not be shared with other clients, and "key
  rejected" is only shown after 5 refusals in a row. Needs an API key (portal); regions are configurable,
  default 31 = Kyiv city. Alert data older than two intervals is not shown as an alert or as all-clear, but as "Air alert
  data … old".
- Partial screen refresh. The screen is only redrawn when something on it changed, and then with a fast,
  flicker-free partial refresh (~0.5 s) instead of the black/white flash. A full refresh still clears ghosting
  every 6 h (configurable, optionally also after N partial refreshes), on a page switch, when the night ends,
  below 12 °C indoors, and on the green button.
- Per-source intervals on one wake grid. The device wakes at the shortest enabled interval, on the clock (:00,
  :03, :06, ... for 3 min), and on each wake refreshes every source that is due by then; no source gets a wake
  of its own. WiFi is only switched on when an online source is due. Defaults:
  - air raid alerts: every 3 min
  - inverter status (battery, grid, charge, load): every 3 min
  - inverter 24 h battery graph: every 15 min, fetching only the part since the last update
  - outage schedule: every 10 min (runs on every 3rd wake, i.e. 9 min)
  - weather: every 30 min
  - indoor sensor: every 3 min, without WiFi
  Outage start/end and the full hour fall on the 3-min grid.
- Night mode, 03:00–08:00 by default: everything refreshes at most every 15 min and there is no hourly full
  refresh. Start, end and interval are configurable, and it can be switched off.
- Connectivity in the status bar: a WiFi icon (crossed out without WiFi, with "!" when WiFi is up but nothing on
  the internet answers), and "No internet for …" and "Inverter offline for …" notices. The latter means Deye
  cloud answers, but the inverter's data logger stopped reporting.
- Green button short press now fetches everything immediately and forces a full refresh.
- Device battery: `charging` in the status bar while the battery reading says the device is on the charger.

### Changed
- The refresh time ("updated HH:MM") is no longer shown in the status bar.
- The device battery's "time since full charge" now counts from the end of the last charge, detected from the
  voltage trend (rise while charging, drop when unplugged). Before, every wake at or above 4.15 V restarted it,
  and a rested full cell stays above that for hours after unplugging, so the time read hours short. The
  "Full-charge voltage" setting is gone. Until the first charge after the update, the time counts from the first
  reading and is marked as an estimate (`80% · ~3d 4h`).
- The drain-per-day figure (`6%/d`) is removed from the status bar.
- Durations in the status bar move in 5-min / 30-min / 1-hour steps, the outage countdown in 5-min (under an
  hour) and 10-min steps, and the device battery and indoor values have hysteresis, so they don't force a
  redraw on every wake.
- Faster online wakes: WiFi reconnects with the last access point and channel (no scan), consecutive requests to
  the same server reuse the TLS connection, and WiFi is off during the panel refresh.
- Update checks run every 12 h (portal setting in hours) plus at power-on, instead of every N wakes.
- Portal: "Wake interval" and the old max-age fields are replaced by the per-source intervals, night mode and
  screen settings above. The outage and inverter intervals start from the new defaults (10 min, 3 min) even if
  they were saved before.
- Offline wakes don't write flash; their state is kept in RTC memory.
- The cache format changed (the SOC graph is cached separately), so the first wake after the update fetches
  everything again.

## [0.3.0] - 2026-10-07

### Added
- Signed over-the-air updates. CI builds every release from its git tag. The
  device installs an image only when its SHA-256 matches the manifest and the
  manifest's ECDSA P-256 signature verifies against the project key
  (`certs/ota_signing_pub.pem`).
- Automatic rollback. A new image stays unconfirmed until it renders a frame
  while online, or until the maintenance portal opens. A crash or reset before
  that boots the previous image, and the rolled-back version is never retried
  automatically.
- The device checks for updates on cold boot as well as every N wakes. It skips
  the download below 3.70 V.
- Maintenance portal:
  - a "Check for update now" button
  - an "update check every N wakes" setting (0 turns the check off)
  - a notice when an update was rolled back
- `factory.bin` release asset (bootloader, partitions, otadata and app in one
  image) for first-time flashing without a build environment.
- GitHub Actions CI (firmware build plus host tests) and a tag-driven release
  workflow with build provenance attestation.

### Changed
- The OTA manifest URL now defaults to this repository's latest release. An
  empty URL saved by an older version falls back to it.
- Versions compare by full SemVer precedence, so `0.3.1-rc.1` sorts before
  `0.3.1`.

### Fixed
- USB uploads wrote the initial otadata (`boot_app0.bin`) at the core's default
  0xe000, which is inside this partition table's NVS, instead of at 0x29000.
  Once a device had taken an OTA update, a USB flash would have kept booting
  the old OTA slot.

## [0.2.0] - 2026-10-07

### Added
- Indoor climate row from the onboard SHT4x sensor. Values outside the comfort
  range are drawn inverted.
- Chance of rain over the next 3 h. The current-weather icon now sits beside
  the temperature.
- A status bar on every page (y 450–480). On the left: clock or WiFi problems,
  stale sources, the next-outage countdown and low battery. On the right: the
  refresh time and the device battery (percent, time since the last full
  charge, drain per day).
- New portal settings: indoor offsets, comfort ranges and the full-charge
  voltage. The portal also shows live battery and sensor readings.

### Changed
- Bolder grid, charge-status and load icons; the SOC graph uses one label font.

### Fixed
- Unchanged cache blobs are no longer rewritten to flash on every fetch.
- Fewer error lines in the boot log; the EPD control pins are claimed before use.

## [0.1.2] - 2026-10-07

### Fixed
Bring-up fixes from the first flash on real hardware:
- 8 MB partition table.
- Switched battery divider (GPIO21).
- SNTP forced on cold boot: the RTC held local time.
- Transparent font rendering.
- "NO OUTAGE DATA" shown for a renumbered Yasno group.
- Deye numbers that arrive as JSON strings.

### Added
- `INSTALL.md` newcomer guide.

## [0.1.0] - 2026-06-11

### Added
- First standalone firmware, ported from the Next.js `/power` dashboard:
  - Open-Meteo weather, Yasno outage schedule, Deye backup power, USD/UAH page
  - deep-sleep wake cycle
  - NVS last-known-good cache with stale badges
  - provisioning and maintenance web portal
  - manual and pull OTA

[Unreleased]: https://github.com/TPAXTOP/e1001-power-dashboard/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/TPAXTOP/e1001-power-dashboard/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/TPAXTOP/e1001-power-dashboard/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/TPAXTOP/e1001-power-dashboard/compare/v0.1.0...v0.1.2
[0.1.0]: https://github.com/TPAXTOP/e1001-power-dashboard/releases/tag/v0.1.0
