#pragma once

// Bump APP_VERSION with each release.
#define APP_VERSION "0.6.0"

// Build stamp, e.g. "Mar  7 2026" and "14:30:00". Defined in
// src/build_stamp.cpp, the only file that uses __DATE__/__TIME__; the
// build_stamp.py pre-script makes it recompile on every build, so the stamp
// is the time of the last build (not of the last change to some file).
// Runtime strings, not literals: no "..." BUILD_DATE concatenation.
extern const char kBuildDate[];
extern const char kBuildTime[];
#define BUILD_DATE kBuildDate
#define BUILD_TIME kBuildTime
