# Changelog

All notable changes to this firmware are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). Devices update themselves to the
newest published (non-pre)release; see [docs/RELEASING.md](docs/RELEASING.md).

## [Unreleased]

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

[Unreleased]: https://github.com/TPAXTOP/e1001-power-dashboard/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/TPAXTOP/e1001-power-dashboard/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/TPAXTOP/e1001-power-dashboard/compare/v0.1.0...v0.1.2
[0.1.0]: https://github.com/TPAXTOP/e1001-power-dashboard/releases/tag/v0.1.0
