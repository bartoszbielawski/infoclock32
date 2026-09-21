#pragma once

#include <string>
#include <ctime>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <data_store.hpp>
#include <runtime_store.hpp>
#include <device_store.hpp>

struct CustomMessage {
    std::string text;
    time_t      start;      // -1 = no lower bound
    time_t      end;        // -1 = no upper bound
    time_t      countdown;  // -1 = plain text, no suffix
};

// Parse "YYYY-MM-DD" → midnight local time_t. Returns -1 on any failure.
inline time_t parse_date(const std::string& s)
{
    if (s.size() < 10) return -1;
    for (int i : {0,1,2,3,5,6,8,9}) if (!isdigit((unsigned char)s[i])) return -1;
    if (s[4] != '-' || s[7] != '-') return -1;

    int year = 0, mon = 0, mday = 0;
    if (sscanf(s.c_str(), "%d-%d-%d", &year, &mon, &mday) != 3) return -1;

    struct tm t = {};
    t.tm_year  = year - 1900;
    t.tm_mon   = mon - 1;
    t.tm_mday  = mday;
    t.tm_isdst = -1;
    time_t result = mktime(&t);
    return (result == (time_t)-1) ? -1 : result;
}

// Signed calendar-day difference between today (local) and the target midnight.
// 0 = the target day itself, >0 = days remaining, <0 = days since the target passed.
// Midnight-to-midnight with rounding keeps DST 23/25-hour days off-by-one-free.
// localtime_r is used because other FreeRTOS tasks call localtime() too.
inline int countdown_days(time_t target, time_t now)
{
    struct tm lt;
    localtime_r(&now, &lt);
    lt.tm_hour  = 0;
    lt.tm_min   = 0;
    lt.tm_sec   = 0;
    lt.tm_isdst = -1;
    time_t today_mid = mktime(&lt);
    return (int)lround(difftime(target, today_mid) / 86400.0);
}

// Start of the day after `midnight` (i.e. end of its day), DST-aware:
// adding tm_mday instead of 86400 seconds handles 23/25-hour switch days.
// Note: in zones where local midnight itself doesn't exist on a switch day,
// mktime() normalizes and lround() absorbs the skew.
inline time_t day_end(time_t midnight)
{
    struct tm lt;
    localtime_r(&midnight, &lt);
    lt.tm_mday  += 1;
    lt.tm_isdst = -1;
    return mktime(&lt);
}

// A countdown slot hides itself from the day after its target,
// unless an explicit end date keeps it alive.
inline bool is_expired(const CustomMessage& m, time_t now)
{
    return m.countdown >= 0 && m.end < 0 && countdown_days(m.countdown, now) < 0;
}

// Match "message_<name>_text" keys; on success `name` holds the slot name.
inline bool parse_message_key(const std::string& key, std::string& name)
{
    static const std::string prefix = "message_";
    static const std::string suffix = "_text";
    if (key.size() < prefix.size() + 1 + suffix.size()) return false;
    if (key.compare(0, prefix.size(), prefix) != 0) return false;
    if (key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    name = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
    return !name.empty();
}

// Expand {key} and {} placeholders.
inline std::string expand_placeholders(const std::string& text, int days, bool has_countdown)
{
    std::string result;
    result.reserve(text.size() + 32);

    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] != '{') { result += text[i++]; continue; }

        size_t end = text.find('}', i + 1);
        if (end == std::string::npos) { result += text[i++]; continue; }

        std::string key = text.substr(i + 1, end - i - 1);

        if (key.empty())
        {
            if (has_countdown)
            {
                char num[12];
                snprintf(num, sizeof(num), "%d", abs(days));
                result += num;
            }
            else
            {
                result += "{}";
            }
        }
        else
        {
            std::string value = RuntimeStore::getInstance().get(key);
            if (value.empty()) value = DeviceStore::getInstance().get(key);
            if (value.empty()) value = DataStore::getInstance().get_value(key, "");
            result += value.empty() ? text.substr(i, end - i + 1) : value;
        }

        i = end + 1;
    }
    return result;
}

// Build the final display string for a message. `now` defaults to the wall
// clock; callers that also apply visibility windows should pass their own
// reading so a midnight rollover can't desync the day count from the filters.
inline std::string build_display(const CustomMessage& m, time_t now = time(nullptr))
{
    bool has_countdown = (m.countdown >= 0);
    int  days          = 0;

    if (has_countdown)
    {
        days = countdown_days(m.countdown, now);
    }

    std::string result = expand_placeholders(m.text, days, has_countdown);

    if (has_countdown && m.text.find("{}") == std::string::npos)
    {
        char suffix[24];
        if (days > 0)       snprintf(suffix, sizeof(suffix), "%dd",  days);
        else if (days == 0) snprintf(suffix, sizeof(suffix), "today!");
        else                snprintf(suffix, sizeof(suffix), "+%dd", -days);
        result += ": ";
        result += suffix;
    }

    return result;
}
