#pragma once
#ifndef INFOCLOCK32_NIGHT_UTILS_HPP
#define INFOCLOCK32_NIGHT_UTILS_HPP

// Shared helpers for the night_start/night_end quiet window.
// Values come from DataStore config keys as "HH:MM"; both unset/invalid
// means night mode is disabled.

#include <ctime>
#include <cstdio>
#include <string>

inline int parse_hhmm(const std::string& s)
{
    int h, m;
    if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59)      return -1;
    return h * 60 + m;
}

inline bool in_night_window(int now_min, int start_min, int end_min)
{
    if (start_min < 0 || end_min < 0) return false;
    if (start_min < end_min)
        return now_min >= start_min && now_min < end_min;
    else // window wraps midnight
        return now_min >= start_min || now_min < end_min;
}

// True when `now` falls inside the configured night window.
template <typename Store>
inline bool is_night_now(Store& ds, time_t now)
{
    int start_min = parse_hhmm(ds.get_value("night_start", ""));
    int end_min   = parse_hhmm(ds.get_value("night_end",   ""));
    if (start_min < 0 || end_min < 0) return false;

    struct tm lt;
    localtime_r(&now, &lt);
    return in_night_window(lt.tm_hour * 60 + lt.tm_min, start_min, end_min);
}

#endif // INFOCLOCK32_NIGHT_UTILS_HPP
