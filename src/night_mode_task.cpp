#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <task_registry.hpp>
#include <freertos/task.h>

#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <night_utils.hpp>
#include <runtime_store.hpp>
#include <night_mode_task.h>

#include <atomic>
#include <string>

static std::atomic<bool> g_night{false};
static std::atomic<int>  g_level{7};

bool night_mode_active() { return g_night; }
int  current_display_brightness() { return g_level; }

void apply_display_brightness(int level)
{
    level = max(0, min(15, level));
    g_level = level;
    // Applied by whoever holds the display on their next frame (see LMDS),
    // so this never waits for the display.
    ResourceManager<LMDS>::getInstance().getResourceRef().requestIntensity((uint8_t)level);
    RuntimeStore::getInstance().set("display_brightness", std::to_string(level));
}

bool set_user_brightness(int level)
{
    level = max(0, min(15, level));
    apply_display_brightness(level);
    // At night this is a temporary override: the configured daytime level
    // must survive, so the next night → day switch restores it.
    if (g_night)
        return false;
    DataStore::getInstance().set_value("brightness", std::to_string(level));
    DataStore::getInstance().save_to_file("/config.txt");
    return true;
}

void night_mode_task(void* parameter)
{
    registerTask("NightMode", 4096, 120000);

    // Let DataStore load and NTP sync before first check.
    vTaskDelay(15000 / portTICK_PERIOD_MS);

    bool last_was_night = false;

    while (true)
    {
        task_heartbeat();
        DataStore& ds = DataStore::getInstance();

        int start_min = parse_hhmm(ds.get_value("night_start", ""));
        int end_min   = parse_hhmm(ds.get_value("night_end",   ""));

        if (start_min < 0 || end_min < 0)
        {
            // Night mode not configured — reset state and idle.
            last_was_night = false;
            g_night = false;
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
            apply_display_brightness(level);
            logPrintf("NGT", "night mode %s → brightness %d",
                      is_night ? "on" : "off", level);
            last_was_night = is_night;
            g_night = is_night;
        }

        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}
