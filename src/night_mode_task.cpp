#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <task_registry.hpp>
#include <freertos/task.h>

#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <data_store.hpp>
#include <logger.hpp>

// Parse "HH:MM" → minutes since midnight, or -1 on failure.
static int parse_hhmm(const std::string& s)
{
    int h, m;
    if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59)      return -1;
    return h * 60 + m;
}

static bool in_night_window(int now_min, int start_min, int end_min)
{
    if (start_min < end_min)
        return now_min >= start_min && now_min < end_min;
    else // window wraps midnight
        return now_min >= start_min || now_min < end_min;
}

void night_mode_task(void* parameter)
{
    registerTask("NightMode", 2048);
    auto& rmd = ResourceManager<LMDS>::getInstance();

    // Let DataStore load and NTP sync before first check.
    vTaskDelay(15000 / portTICK_PERIOD_MS);

    bool last_was_night = false;

    while (true)
    {
        DataStore& ds = DataStore::getInstance();

        int start_min = parse_hhmm(ds.get_value("night_start", ""));
        int end_min   = parse_hhmm(ds.get_value("night_end",   ""));

        if (start_min < 0 || end_min < 0)
        {
            // Night mode not configured — reset state and idle.
            last_was_night = false;
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        int now_min = t->tm_hour * 60 + t->tm_min;

        bool is_night = in_night_window(now_min, start_min, end_min);

        if (is_night != last_was_night)
        {
            int level = is_night
                ? ds.get_value("night_brightness", 1)
                : ds.get_value("brightness",        7);
            level = max(0, min(15, level));

            if (auto display = rmd.acquire())
            {
                display->setIntensity((uint8_t)level);
                logPrintf("NGT", "night mode %s → brightness %d",
                          is_night ? "on" : "off", level);
            }
            last_was_night = is_night;
        }

        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}
