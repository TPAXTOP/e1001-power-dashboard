# Contributing

Thanks for your interest! This is a small hobby project for one specific device (Seeed reTerminal E1001), so please
open an issue first for anything bigger than a bug fix.

## Setup

```powershell
pip install -r requirements-dev.txt
python -m platformio run -e e1001        # firmware
python -m platformio test -e native      # host unit tests (needs gcc/MinGW on PATH)
```

CI runs both on every push and pull request.

## Ground rules

- **Display logic lives in `lib/dashcore/`** as pure C++ with host tests. `derive.cpp` mirrors the original web
  dashboard and must keep the same semantics. Firmware-only logic goes in `status.cpp` / `semver.cpp`.
- **Bump `dash::kCacheVersion`** whenever a struct in `data_model.h` changes layout.
- **No secrets in code.** Every credential is runtime configuration stored in NVS.
- **Pages must leave y ≥ 450 free**, because the status bar draws there.
- Match the existing style: 2-space indent, 100 columns, comments that explain *why*.
- Add a line under `[Unreleased]` in `CHANGELOG.md` for any user-visible change.

## Releases

Maintainers only. See [docs/RELEASING.md](docs/RELEASING.md).
