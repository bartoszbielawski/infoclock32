#include <Arduino.h>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <string>
#include <vector>
#include <ctime>

static constexpr int MAX_SLOTS       = 8;
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
    char key[24];

    for (int i = 1; i <= MAX_SLOTS; i++)
    {
        snprintf(key, sizeof(key), "msg%d_text", i);
        std::string text = ds.get_value(key, "");
        if (text.empty()) continue;

        CustomMessage m;
        m.text = text;

        snprintf(key, sizeof(key), "msg%d_start", i);
        m.start = parse_date(ds.get_value(key, ""));

        snprintf(key, sizeof(key), "msg%d_end", i);
        m.end = parse_date(ds.get_value(key, ""));

        snprintf(key, sizeof(key), "msg%d_countdown", i);
        m.countdown = parse_date(ds.get_value(key, ""));

        out.push_back(m);
    }
    return out;
}

// Build the display string from a message and its countdown date.
//
// Placeholder mode — text contains "{}":
//   {} is replaced with the absolute number of days to/from the target.
//   "LS3 will start in {} days!"  →  "LS3 will start in 45 days!"
//   "LS3 started {} days ago!"   →  "LS3 started 3 days ago!"
//   (Use msg<N>_end / msg<N>_start to gate which variant is visible.)
//
// Append mode — no "{}" in text:
//   A suffix is appended automatically.
//   Future → "Event: 45d"
//   Today  → "Event: today!"
//   Past   → "Event: +5d"
static std::string build_display(const CustomMessage& m)
{
    if (m.countdown < 0) return m.text;

    time_t now  = time(nullptr);
    long diff_s = (long)difftime(m.countdown, now);
    int  days   = (int)(diff_s / 86400);   // negative when past

    // Placeholder mode: replace "{}" with the absolute day count
    auto pos = m.text.find("{}");
    if (pos != std::string::npos)
    {
        char num[12];
        snprintf(num, sizeof(num), "%d", abs(days));
        std::string result = m.text;
        result.replace(pos, 2, num);
        return result;
    }

    // Append mode: auto-scaled suffix
    char suffix[24];
    if (days > 0)
        snprintf(suffix, sizeof(suffix), "%dd", days);
    else if (days == 0)
        snprintf(suffix, sizeof(suffix), "today!");
    else
        snprintf(suffix, sizeof(suffix), "+%dd", -days);

    return m.text + ": " + suffix;
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
            Serial.printf("CustomMsg: %s\n", display.c_str());

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
