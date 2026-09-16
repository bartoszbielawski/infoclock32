#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <ctime>

#include <resource_manager.hpp>
#include <task_registry.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <sun_times.hpp>

static constexpr int DEFAULT_INTERVAL_MIN = 30;

// Scrolls "Sunrise HH:MM  Sunset HH:MM" as a recurring display widget.
// Disabled unless sun_lat and sun_lon are configured; recomputes when the
// local date (or the configured position) changes.
void sun_times_task(void* /*parameter*/)
{
    registerTask("SunTimes", 4096, 120000);
    auto& rmd = ResourceManager<LMDS>::getInstance();
    auto& ds = DataStore::getInstance();

    // Let NTP and apply_timezone() settle before the first computation.
    vTaskDelay(20000 / portTICK_PERIOD_MS);

    char line[48];
    int  computedDay = -1;
    double computedLat = NAN, computedLon = NAN;
    bool haveLine = false;

    while (true)
    {
        task_heartbeat();
        double lat = ds.get_value<float>("sun_lat", NAN);
        double lon = ds.get_value<float>("sun_lon", NAN);

        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);

        if (std::isfinite(lat) && std::isfinite(lon) &&
            (lt.tm_mday != computedDay || lat != computedLat || lon != computedLon))
        {
            SunTimes st = compute_sun_times(lat, lon,
                                            lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
            switch (st.kind)
            {
                case SunTimes::Kind::Normal:
                    snprintf(line, sizeof(line), "Sunrise %02d:%02d  Sunset %02d:%02d",
                             st.sunrise_min / 60, st.sunrise_min % 60,
                             st.sunset_min / 60, st.sunset_min % 60);
                    haveLine = true;
                    break;
                case SunTimes::Kind::PolarDay:
                    snprintf(line, sizeof(line), "Sun: polar day!");
                    haveLine = true;
                    break;
                case SunTimes::Kind::PolarNight:
                    snprintf(line, sizeof(line), "Sun: polar night!");
                    haveLine = true;
                    break;
            }
            computedDay = lt.tm_mday;
            computedLat = lat;
            computedLon = lon;
            logPrintf("SUN", "%s", haveLine ? line : "no data");
        }

        int interval_min = max(5, min(720,
            ds.get_value<int>("sun_interval_min", DEFAULT_INTERVAL_MIN)));

        if (haveLine)
        {
            // Block (not timeout-acquire): a timed-out request would linger
            // in the manager queue as a dead entry and poison the handshake.
            if (auto display = rmd.acquire())
                scrollMessage(line, display, 30);
            else
                logPrintf("SUN", "display busy, skipping cycle");
        }

        // Sleep in 1-minute ticks so config changes take effect promptly.
        task_heartbeat_grace(interval_min * 60000 + 25000);  // + display wait and scroll
        for (int m = 0; m < interval_min; m++)
            vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}
