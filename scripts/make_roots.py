#!/usr/bin/env python3
"""Extract a curated set of root CAs from a Mozilla cacert.pem into certs/roots.pem.

The firmware embeds certs/roots.pem and feeds it to mbedTLS via setCACert().
A curated subset (~15 roots) keeps per-handshake parse time and RAM low while
covering the certificate chains of every host the firmware talks to:
Open-Meteo / Yasno / exchangerate.host (Let's Encrypt, Google Trust, DigiCert,
Cloudflare), Deye Cloud (DigiCert / GlobalSign / Amazon), GitHub releases
(DigiCert / Sectigo), NTP-over-TLS not used.

Usage:
  python scripts/make_roots.py path/to/cacert.pem
Regenerate yearly or when a host's chain changes (symptom: TLS error -0x2700
in serial log -> rerun with current cacert.pem, check host root with
scripts/check_roots.ps1).
"""
import re
import sys
from pathlib import Path

WANTED = [
    "ISRG Root X1",
    "ISRG Root X2",
    "DigiCert Global Root G2",
    "DigiCert Global Root G3",
    "DigiCert Trusted Root G4",
    "DigiCert TLS RSA4096 Root G5",
    "GTS Root R1",
    "GTS Root R3",
    "GTS Root R4",
    "GlobalSign Root CA - R3",
    "GlobalSign Root R46",
    "Amazon Root CA 1",
    "Amazon Root CA 3",
    "USERTrust RSA Certification Authority",
    "USERTrust ECC Certification Authority",
    "Sectigo Public Server Authentication Root R46",
    "Sectigo Public Server Authentication Root E46",
    "GlobalSign ECC Root CA - R4",
    "GlobalSign ECC Root CA - R5",
    "GlobalSign Root E46",
    "Starfield Services Root Certificate Authority",
    "SSL.com TLS RSA Root CA 2022",
    "COMODO RSA Certification Authority",
]


def main() -> None:
    src = Path(sys.argv[1] if len(sys.argv) > 1 else "cacert.pem")
    out = Path(__file__).resolve().parent.parent / "certs" / "roots.pem"
    text = src.read_text(encoding="utf-8")

    # cacert.pem entries: title line, ===== underline, then PEM block
    entries = re.findall(
        r"^([^\n=]+)\n=+\n(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)",
        text,
        re.S | re.M,
    )
    by_title = {title.strip(): pem for title, pem in entries}

    picked, missing = [], []
    for name in WANTED:
        hit = by_title.get(name)
        if hit is None:
            # fall back to substring match (titles shift slightly across releases)
            for title, pem in by_title.items():
                if name.lower() in title.lower():
                    hit, name = pem, title
                    break
        if hit:
            picked.append(f"# {name}\n{hit}\n")
        else:
            missing.append(name)

    out.write_text("".join(picked), encoding="utf-8", newline="\n")
    print(f"wrote {out} with {len(picked)} roots ({out.stat().st_size} bytes)")
    if missing:
        print("WARNING, not found:", ", ".join(missing))


if __name__ == "__main__":
    main()
