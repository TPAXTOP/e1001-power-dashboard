# Security policy

## Reporting a vulnerability

Please **do not open a public issue** for security problems. Use GitHub's private reporting instead:
[Report a vulnerability](https://github.com/TPAXTOP/e1001-power-dashboard/security/advisories/new).
Expect an answer within a week. Fixes ship as a normal release, which devices install by themselves.

Only the latest release is supported.

## How firmware updates are protected

Devices update themselves over WiFi from this repository's GitHub Releases. Each step of that chain has a safeguard:

| Step | Protection |
|---|---|
| Building | Releases are built by GitHub Actions from a tagged commit, never on a laptop. The build job has no access to secrets, and each binary carries a [build provenance attestation](https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations) (`gh attestation verify firmware.bin --repo TPAXTOP/e1001-power-dashboard`) |
| Signing | A separate job in the protected `release` environment, which only `v*` tags can use, signs `"<version>\n<sha256>\n"` with an ECDSA P-256 key. The key exists only as an environment secret plus an offline backup |
| Publishing | Release tags can't be moved or deleted, and releases are immutable once published |
| Transport | HTTPS with a curated root CA store (`certs/roots.pem`) |
| Device check | The device downloads into the inactive OTA slot and activates it only if the size and SHA-256 match the manifest **and** the signature verifies against the public key compiled into the running firmware (`certs/ota_signing_pub.pem`). Versions must be strictly newer (SemVer), so a validly signed old image can't be replayed as a downgrade |
| After install | The new image stays unconfirmed until it draws the dashboard while online (or the maintenance portal opens). Any crash or reset before that makes the bootloader return to the previous image, and that version isn't retried automatically |

Taking over the GitHub account or the CDN is therefore not enough to push firmware to devices. That would also need
the signing key.

### Deliberate exceptions

- **Manual upload in the maintenance portal accepts unsigned images.** It is the path for your own builds and for
  recovery. Reaching it requires holding the device's button and being on the same network. The portal has no
  password, and it closes after 10 minutes of inactivity.
- **No hardware Secure Boot or flash encryption.** Someone with physical USB access can read or replace the
  firmware. That includes the WiFi and API credentials stored in NVS.

### Secrets

The repository holds no credentials. WiFi, Deye and exchangerate.host credentials are entered in the device portal
and stored in its NVS. The Deye password is stored only as SHA-256.
