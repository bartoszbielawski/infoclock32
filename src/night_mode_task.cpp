#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <task_registry.hpp>
#include <freertos/task.h>

#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <night_utils.hpp>

void night_mode_task(void* parameter)
{
    registerTask("NightMode", 4096, 120000);
    auto& rmd = ResourceManager<LMDS>::getInstance();

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

            // Brightness-only change: a 500 ms attempt mirrors the MQTT
            // brightness path. last_was_night is only latched on success, so a
            // busy display just retries on the next 60 s cycle instead of
            // blocking this task (or a holder) for a full display hold.
            if (auto display = rmd.acquire(pdMS_TO_TICKS(500)))
            {
                display->setIntensity((uint8_t)level);
                logPrintf("NGT", "night mode %s → brightness %d",
                          is_night ? "on" : "off", level);
                last_was_night = is_night;
            }
            else
            {
                logPrintf("NGT", "display busy, brightness change deferred");
            }
        }

        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}
