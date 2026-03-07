#pragma once

// Bump APP_VERSION with each release.
#define APP_VERSION "1.0.0"

// BUILD_DATE / BUILD_TIME are injected by the compiler at compile time.
// Example values: "Mar  7 2026"  and  "14:30:00"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__
