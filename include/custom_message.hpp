#pragma once

#include <string>
#include <ctime>
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

// Build the final display string for a message.
inline std::string build_display(const CustomMessage& m)
{
    bool has_countdown = (m.countdown >= 0);
    int  days          = 0;

    if (has_countdown)
    {
        time_t now  = time(nullptr);
        long diff_s = (long)difftime(m.countdown, now);
        days = (int)(diff_s / 86400);
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
