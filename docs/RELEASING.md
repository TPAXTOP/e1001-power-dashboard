# Releasing

Releases are tag-driven. You never build or upload binaries by hand.

## Cut a release

1. Make sure `main` is green in CI, and that `CHANGELOG.md` describes the changes under `## [Unreleased]`.
2. Run:

   ```powershell
   python scripts/release.py 0.3.1
   git push origin main --follow-tags
   ```

   `release.py` checks that the tree is clean and on `main`. It then bumps `APP_VERSION` in `include/version.h`,
   turns `[Unreleased]` into `[0.3.1] - <date>`, commits `Release v0.3.1` and creates an annotated tag. Nothing is
   pushed until you push.
3. The tag starts the [Release workflow](../.github/workflows/release.yml):
   - **build** checks that the tag matches `APP_VERSION`, runs the host tests, builds `firmware.bin` and
     `factory.bin`. It has no secrets.
   - **publish** runs in the `release` environment. It signs `version.json` with `OTA_SIGNING_KEY`, re-verifies it
     against `certs/ota_signing_pub.pem`, attests build provenance, and publishes the GitHub release with the
     CHANGELOG section as its notes.
4. Devices install it on their next check: cold boot, every `otaEveryN` wakes (about 12 h), or **Check for update
   now** in the portal.

Release assets:

| File | Purpose |
|---|---|
| `firmware.bin` | App image that devices download over the air. Also what the portal's manual upload takes |
| `factory.bin` | Bootloader, partitions, otadata and app; `esptool write-flash 0x0 factory.bin` |
| `version.json` | OTA manifest `{version, url, sha256, size, sig}` |
| `SHA256SUMS` | Checksums of both binaries |

## Test a release candidate first

```powershell
python scripts/release.py 0.4.0-rc.1
git push origin main --follow-tags
```

A version with `-` is published as a GitHub **pre-release**. Its notes come from `[Unreleased]`, which stays in
place for the final release. `releases/latest` skips pre-releases, so no device
picks it up automatically. To try it on your own device:

- **Portal:** set *Update manifest URL* to
  `https://github.com/TPAXTOP/e1001-power-dashboard/releases/download/v0.4.0-rc.1/version.json`, save, then click
  **Check for update now**. Clear the field afterwards so the device follows `latest` again.
- **Or USB:** flash the rc's `factory.bin`, or upload its `firmware.bin` in the portal.

SemVer precedence applies, so `0.4.0-rc.1 < 0.4.0`. A device on the rc upgrades to the final `0.4.0` by itself.

## What the device does with a new version

1. It fetches `version.json`. It ignores the manifest when the signature is missing or invalid, when the version
   isn't newer than its own, or when the version is the one recorded as rolled back.
2. It streams `firmware.bin` into the inactive OTA slot while hashing it. It activates the slot only if the size,
   SHA-256 and signature all check out.
3. It reboots into the new image, which is *pending verification*. The image confirms itself after it draws the
   dashboard while online. When offline, it retries up to 3 times, 60 s apart, before sleeping. Opening the
   maintenance portal also confirms it.
4. If the image crashes or resets before confirming, the bootloader starts the previous image. The previous image
   records the version as bad and won't retry it automatically.

## If something goes wrong

- **Bad release already published:** don't delete it. Fix forward with a higher version, e.g. `0.3.2`. Tags and
  releases are immutable, and a device that rolled back skips the bad version anyway.
- **Workflow failed before publishing:** fix the cause on `main`. Then delete the tag locally
  (`git tag -d v0.3.1`) and cut the next patch version. The tag ruleset doesn't allow moving a pushed tag.
- **Device can't be reached at all:** USB recovery. Press green, then run `python -m platformio run -e e1001 -t
  upload`, or flash `factory.bin` (after `esptool erase-flash`, but that wipes settings).

## The signing key

- Public half: `certs/ota_signing_pub.pem`, compiled into every firmware.
- Private half: the `OTA_SIGNING_KEY` secret of the `release` environment, plus the maintainer's offline backup.
- **Lost key:** devices can no longer update over the air with new releases. Generate a new pair, commit the new
  public key, release, and install that release once on each device by hand (portal upload or USB).
- **Leaked key:** treat it as an incident. Rotate the key the same way. Until each device has received the new
  public key, someone who also gets a manifest in front of the device could push firmware to it.
- Rotating on purpose, without losing OTA: release one version that is signed with the **old** key but contains the
  **new** public key. Every later release is signed with the new key.
