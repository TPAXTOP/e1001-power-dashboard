#!/usr/bin/env python3
"""Package an OTA release: copy the built images and write a signed version.json.

Usage (after `pio run -e e1001` and `pio run -e e1001 -t factory`):
  python scripts/gen_version.py <base-url> --sign [--key private.pem]
  python scripts/gen_version.py --verify          # check release/ against the public key

<base-url> is where the assets will live, e.g.
  https://github.com/TPAXTOP/e1001-power-dashboard/releases/download/v0.3.0

The release workflow (.github/workflows/release.yml) runs this; by hand you
only need it to reproduce a release. Reads APP_VERSION from include/version.h
and produces release/:
  firmware.bin   - app image, what devices download over the air
  factory.bin    - bootloader + partitions + otadata + app, flashes at 0x0
  version.json   - {"version", "url", "sha256", "size", "sig"} manifest
  SHA256SUMS     - checksums of the binaries

The signature is ECDSA P-256 / SHA-256 over "<version>\\n<sha256hex>\\n", DER,
base64. The private key comes from --key or the OTA_SIGNING_KEY environment
variable (PEM text). Devices refuse manifests whose signature does not verify
against certs/ota_signing_pub.pem. Requires `pip install cryptography`.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / ".pio" / "build" / "e1001"
OUT = ROOT / "release"
PUBKEY = ROOT / "certs" / "ota_signing_pub.pem"


def app_version() -> str:
    version_h = (ROOT / "include" / "version.h").read_text(encoding="utf-8")
    m = re.search(r'#define APP_VERSION "([^"]+)"', version_h)
    if not m:
        sys.exit("APP_VERSION not found in include/version.h")
    return m.group(1)


def signed_message(version: str, sha_hex: str) -> bytes:
    return f"{version}\n{sha_hex}\n".encode()


def load_private_key(path: str | None):
    from cryptography.hazmat.primitives import serialization

    if path:
        pem = Path(path).read_bytes()
    elif os.environ.get("OTA_SIGNING_KEY"):
        pem = os.environ["OTA_SIGNING_KEY"].encode()
    else:
        sys.exit("--sign needs --key <file> or the OTA_SIGNING_KEY environment variable")
    return serialization.load_pem_private_key(pem, password=None)


def sign(key, message: bytes) -> str:
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec

    return base64.b64encode(key.sign(message, ec.ECDSA(hashes.SHA256()))).decode()


def verify_release() -> None:
    """Re-check release/ the way a device would: hash, size and signature."""
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec

    manifest = json.loads((OUT / "version.json").read_text(encoding="utf-8"))
    image = (OUT / "firmware.bin").read_bytes()
    sha = hashlib.sha256(image).hexdigest()
    if sha != manifest["sha256"] or len(image) != manifest["size"]:
        sys.exit("verify: firmware.bin does not match version.json")
    pub = serialization.load_pem_public_key(PUBKEY.read_bytes())
    try:
        pub.verify(
            base64.b64decode(manifest["sig"]),
            signed_message(manifest["version"], sha),
            ec.ECDSA(hashes.SHA256()),
        )
    except InvalidSignature:
        sys.exit(f"verify: signature does not match {PUBKEY.relative_to(ROOT)}")
    print(f"verify: version.json {manifest['version']} OK (hash, size, signature)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("base_url", nargs="?", default="", help="URL the assets are published under")
    ap.add_argument("--sign", action="store_true", help="sign the manifest (required for OTA)")
    ap.add_argument("--key", help="private key PEM file (default: $OTA_SIGNING_KEY)")
    ap.add_argument("--verify", action="store_true", help="only verify an existing release/")
    args = ap.parse_args()

    if args.verify:
        verify_release()
        return

    version = app_version()
    firmware = BUILD / "firmware.bin"
    if not firmware.exists():
        sys.exit(f"{firmware} not found - run `pio run -e e1001` first")

    OUT.mkdir(exist_ok=True)
    shutil.copyfile(firmware, OUT / "firmware.bin")
    binaries = ["firmware.bin"]
    factory = BUILD / "factory.bin"
    if factory.exists():
        shutil.copyfile(factory, OUT / "factory.bin")
        binaries.append("factory.bin")
    else:
        print("note: no factory.bin (run `pio run -e e1001 -t factory`)")

    image = (OUT / "firmware.bin").read_bytes()
    sha = hashlib.sha256(image).hexdigest()
    base_url = args.base_url.rstrip("/")
    manifest = {
        "version": version,
        "url": f"{base_url}/firmware.bin" if base_url else "firmware.bin",
        "sha256": sha,
        "size": len(image),
    }
    if args.sign:
        manifest["sig"] = sign(load_private_key(args.key), signed_message(version, sha))
    else:
        print("note: unsigned manifest - devices will refuse it")
    (OUT / "version.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    sums = "".join(
        f"{hashlib.sha256((OUT / name).read_bytes()).hexdigest()}  {name}\n" for name in binaries
    )
    (OUT / "SHA256SUMS").write_text(sums, encoding="utf-8")

    print(f"release/firmware.bin  {len(image)} bytes, sha256 {sha[:16]}...")
    print(f"release/version.json  version {version}{', signed' if args.sign else ''}")
    if args.sign:
        verify_release()


if __name__ == "__main__":
    main()
