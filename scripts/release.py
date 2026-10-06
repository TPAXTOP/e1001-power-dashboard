#!/usr/bin/env python3
"""Cut a release: bump APP_VERSION, date the changelog, commit and tag.

Usage:
  python scripts/release.py 0.3.1          # stable: every device picks it up
  python scripts/release.py 0.4.0-rc.1     # prerelease: GitHub "pre-release",
                                           # devices only get it on purpose

Then publish with:
  git push origin main --follow-tags

Pushing the tag starts .github/workflows/release.yml, which builds, tests,
signs and publishes the GitHub release. Devices install it on their next
update check. Nothing is pushed by this script, so you can still inspect the
commit (`git show`) or undo it (`git tag -d vX.Y.Z && git reset --hard HEAD~1`).
"""
import datetime
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_H = ROOT / "include" / "version.h"
CHANGELOG = ROOT / "CHANGELOG.md"
REPO_URL = "https://github.com/TPAXTOP/e1001-power-dashboard"
SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)


def git(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout.strip()


def main() -> None:
    if len(sys.argv) != 2 or not SEMVER.match(sys.argv[1].lstrip("v")):
        sys.exit(__doc__)
    version = sys.argv[1].lstrip("v")
    tag = f"v{version}"

    if git("rev-parse", "--abbrev-ref", "HEAD") != "main":
        sys.exit("release from the main branch")
    if git("status", "--porcelain"):
        sys.exit("working tree is not clean - commit or stash first")
    if git("tag", "--list", tag):
        sys.exit(f"tag {tag} already exists")

    current = re.search(r'#define APP_VERSION "([^"]+)"', VERSION_H.read_text()).group(1)
    print(f"{current} -> {version}")

    changelog = CHANGELOG.read_text(encoding="utf-8")
    unreleased = re.search(r"## \[Unreleased\]\n(.*?)(?=\n## \[)", changelog, re.S)
    if not unreleased or not unreleased.group(1).strip():
        sys.exit("CHANGELOG.md has nothing under [Unreleased] - describe the release first")

    # A prerelease leaves [Unreleased] in place (the workflow uses it as the
    # notes), so the final release still finds its changes there.
    if "-" not in version:
        today = datetime.date.today().isoformat()
        changelog = changelog.replace(
            "## [Unreleased]\n", f"## [Unreleased]\n\n## [{version}] - {today}\n", 1
        )
        # Link references at the bottom: Unreleased compares from the new tag.
        prev = re.search(r"^\[Unreleased\]: .*/compare/(\S+)\.\.\.HEAD$", changelog, re.M)
        if prev:
            changelog = changelog.replace(
                prev.group(0),
                f"[Unreleased]: {REPO_URL}/compare/{tag}...HEAD\n"
                f"[{version}]: {REPO_URL}/compare/{prev.group(1)}...{tag}",
            )
        CHANGELOG.write_text(changelog, encoding="utf-8")

    VERSION_H.write_text(
        re.sub(
            r'#define APP_VERSION "[^"]+"',
            f'#define APP_VERSION "{version}"',
            VERSION_H.read_text(encoding="utf-8"),
        ),
        encoding="utf-8",
    )

    git("add", str(VERSION_H), str(CHANGELOG))
    git("commit", "-q", "-m", f"Release {tag}")
    git("tag", "-a", tag, "-m", f"Release {tag}")
    print(f"Committed and tagged {tag}. Publish with:\n  git push origin main --follow-tags")


if __name__ == "__main__":
    main()
