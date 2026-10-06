// SemVer 2.0 precedence for the OTA version check (firmware-only, no web app
// counterpart). Pure C++, host-unit-tested.
#pragma once

namespace dash {

// True for MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD], e.g. "0.3.1-rc.1".
bool semverValid(const char* v);

// <0 when a < b, 0 for equal precedence, >0 when a > b. A prerelease sorts
// before its release (0.3.1-rc.1 < 0.3.1); build metadata is ignored.
// Both arguments must be semverValid().
int semverCompare(const char* a, const char* b);

}  // namespace dash
