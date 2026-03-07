#include <Arduino.h>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <runtime_store.hpp>
#include <logger.hpp>
#include <string>
#include <vector>
#include <ctime>

static constexpr int DEFAULT_CYCLE_S = 60;

// Parse "YYYY-MM-DD" → midnight local time_t. Returns -1 on any failure.
static time_t parse_date(const std::string& s)
{
    if (s.size() < 10) return -1;
    // Expect digits at positions 0-3, 5-6, 8-9 and dashes at 4 and 7
    for (int i : {0,1,2,3,5,6,8,9}) if (!isdigit((unsigned char)s[i])) return -1;
    if (s[4] != '-' || s[7] != '-') return -1;

    struct tm t = {};
    t.tm_year  = std::stoi(s.substr(0, 4)) - 1900;
    t.tm_mon   = std::stoi(s.substr(5, 2)) - 1;
    t.tm_mday  = std::stoi(s.substr(8, 2));
    t.tm_isdst = -1;
    time_t result = mktime(&t);
    return (result == (time_t)-1) ? -1 : result;
}

struct CustomMessage {
    std::string text;
    time_t      start;      // -1 = no lower bound
    time_t      end;        // -1 = no upper bound
    time_t      countdown;  // -1 = plain text, no suffix
};

static std::vector<CustomMessage> load_messages()
{
    std::vector<CustomMessage> out;
    auto& ds = DataStore::getInstance();

    // Collect all keys of the form "message_<name>_text"
    auto keys = ds.get_keys_with_prefix("message_");
    for (const auto& key : keys)
    {
        // Must end with "_text"
        const std::string suffix = "_text";
        if (key.size() < 8 + 1 + suffix.size()) continue;
        if (key.substr(key.size() - suffix.size()) != suffix) continue;

        // Extract slot name: between "message_" (8) and "_text" (5)
        std::string name = key.substr(8, key.size() - 13);
        std::string text = ds.get_value(key, "");
        if (text.empty()) continue;

        CustomMessage m;
        m.text      = text;
        m.start     = parse_date(ds.get_value("message_" + name + "_start",     ""));
        m.end       = parse_date(ds.get_value("message_" + name + "_end",       ""));
        m.countdown = parse_date(ds.get_value("message_" + name + "_countdown", ""));
        out.push_back(m);
    }
    return out;
}

// Expand all {…} placeholders in one pass over the text.
//
//   {}        → abs countdown day count (only when has_countdown is true;
//                left as "{}" if no countdown is configured)
//   {key}     → DataStore value for "key" (left as "{key}" if key absent)
//   unmatched → passed through literally
//
static std::string expand_placeholders(const std::string& text, int days, bool has_countdown)
{
    std::string result;
    result.reserve(text.size() + 32);

    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] != '{') { result += text[i++]; continue; }

        size_t end = text.find('}', i + 1);
        if (end == std::string::npos) { result += text[i++]; continue; }  // unmatched '{'

        std::string key = text.substr(i + 1, end - i - 1);

        if (key.empty())
        {
            // {} → countdown days
            if (has_countdown)
            {
                char num[12];
                snprintf(num, sizeof(num), "%d", abs(days));
                result += num;
            }
            else
            {
                result += "{}";  // no countdown — pass through
            }
        }
        else
        {
            // {key} → RuntimeStore first (live readings), then DataStore (config)
            std::string value = RuntimeStore::getInstance().get(key);
            if (value.empty()) value = DataStore::getInstance().get_value(key, "");
            result += value.empty() ? text.substr(i, end - i + 1) : value;
        }

        i = end + 1;
    }
    return result;
}

// Build the display string from a message and its countdown date.
//
// Placeholder mode — text contains "{}":
//   {} is replaced with the absolute number of days to/from the target.
//   "LS3 will start in {} days!"  →  "LS3 will start in 45 days!"
//   (Use start/end dates to gate which variant is visible.)
//
// Named placeholder mode — text contains "{key}":
//   {key} is replaced with the DataStore value for "key".
//   "Beam in {} days - {location}"  →  "Beam in 12 days - Geneva"
//
// Append mode — no "{}" in text but countdown is set:
//   A suffix is appended automatically.
//   Future → "Event: 45d"  |  Today → "Event: today!"  |  Past → "Event: +5d"
//
// All three modes compose: named placeholders are expanded first, then countdown.
static std::string build_display(const CustomMessage& m)
{
    bool has_countdown = (m.countdown >= 0);
    int  days          = 0;

    if (has_countdown)
    {
        time_t now  = time(nullptr);
        long diff_s = (long)difftime(m.countdown, now);
        days = (int)(diff_s / 86400);   // negative = past
    }

    std::string result = expand_placeholders(m.text, days, has_countdown);

    // Append mode: countdown set but no "{}" was present in the original text
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

void custom_message_task(void* /*parameter*/)
{
    auto& rmd = ResourceManager<LMDS>::getInstance();

    while (true)
    {
        auto messages = load_messages();
        time_t now    = time(nullptr);

        int cycle_s = DEFAULT_CYCLE_S;
        std::string iv = DataStore::getInstance().get_value("msg_interval", "");
        if (!iv.empty()) {
            int v = atoi(iv.c_str());
            if (v >= 10) cycle_s = v;
        }

        for (const auto& m : messages)
        {
            // start is inclusive (>= midnight of start day)
            if (m.start >= 0 && now < m.start) continue;
            // end is inclusive — show through end of the end day
            if (m.end   >= 0 && now >= m.end + 86400) continue;

            std::string display = build_display(m);
            logPrintf("MSG", "%s", display.c_str());

            if (!rmd.make_access_request())
            {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }

            auto& matrix = rmd.getResourceRef();
            scrollMessage(display, matrix, 50);
            rmd.release_access();

            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }

        vTaskDelay((cycle_s * 1000) / portTICK_PERIOD_MS);
    }
}
