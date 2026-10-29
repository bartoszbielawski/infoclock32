#pragma once
#include <ctime>

// Until SNTP has synced, time() counts from the 1970 epoch. Anything derived
// from it (date, day name, countdowns, night window, sun times) is garbage, so
// time-dependent tasks check this first. 1700000000 = 2023-11-14, safely after
// the oldest firmware build and long before any real clock reading.
constexpr time_t kMinValidTime = 1700000000;

inline bool time_is_valid(time_t t) { return t >= kMinValidTime; }

inline bool wall_time_valid() { return time_is_valid(time(nullptr)); }
