#!/usr/bin/env python3
"""Package an OTA release: copies the built firmware and writes version.json.

Usage (after `pio run -e e1001`):
  python scripts/gen_version.py https://github.com/<user>/<repo>/releases/download/v<X.Y.Z>

Reads APP_VERSION from include/version.h, then produces release/:
  firmware.bin   - the app image to upload as a release asset
  version.json   - {"version", "url", "sha256"} manifest; upload it to the
                   release too, and point the device's "OTA manifest URL" at
                   .../releases/latest/download/version.json

The device updates itself on its periodic check once both files are published.
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    base_url = sys.argv[1].rstrip("/") if len(sys.argv) > 1 else ""

    version_h = (ROOT / "include" / "version.h").read_text(encoding="utf-8")
    m = re.search(r'#define APP_VERSION "([^"]+)"', version_h)
    if not m:
        sys.exit("APP_VERSION not found in include/version.h")
    version = m.group(1)

    built = ROOT / ".pio" / "build" / "e1001" / "firmware.bin"
    if not built.exists():
        sys.exit(f"{built} not found - run `pio run -e e1001` first")

    out = ROOT / "release"
    out.mkdir(exist_ok=True)
    target = out / "firmware.bin"
    shutil.copyfile(built, target)

    sha = hashlib.sha256(target.read_bytes()).hexdigest()
    manifest = {
        "version": version,
        "url": f"{base_url}/firmware.bin" if base_url else "firmware.bin",
        "sha256": sha,
        "size": target.stat().st_size,
    }
    (out / "version.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"release/firmware.bin  {manifest['size']} bytes")
    print(f"release/version.json  version {version}, sha256 {sha[:16]}...")


if __name__ == "__main__":
    main()
