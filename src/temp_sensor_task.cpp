#include <Arduino.h>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <task_registry.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <runtime_store.hpp>
#include <logger.hpp>
#include <temp_sensor.hpp>
#include <pressure_history.hpp>
#include <temp_display.hpp>
#include <string>

// Default display update interval in seconds
static constexpr int DEFAULT_INTERVAL_S = 300;

// Config keys
static const char CFG_INTERVAL[]      = "temp_interval";
static const char CFG_TEMP_OFFSET[]   = "temp_offset";
static const char CFG_DISPLAY[]       = "temp_display";
static const char CFG_DECIMALS[]      = "temp_decimals";
static const char CFG_SHOW_INTERVAL[] = "temp_show_interval";
static const char CFG_HOLD[]          = "temp_hold_s";

void temp_sensor_task(void* parameter)
{
    registerTask("TempSensor", 4096, 60000);
    TempSensor* sensor = static_cast<TempSensor*>(parameter);

    logPrintf("TMP", "starting with sensor '%s'", sensor->name());

    if (!sensor->begin())
    {
        logPrintf("TMP", "sensor '%s' failed to initialize, task exiting", sensor->name());
        task_mark_exited();   // stop watchdog supervision before deleting the task
        vTaskDelete(nullptr);
        return;
    }

    auto& rmd = ResourceManager<LMDS>::getInstance();
    time_t last_update = 0;
    time_t last_display = 0;

    float temp = 0.0f;
    float pressure = 0.0f;
    float humidity = 0.0f;

    // Pressure trend (valid only when the sensor provides pressure)
    bool haveTrend = false;
    TrendKind trendKind = TREND_STEADY;
    float trendRate = 0.0f;

    bool read_success = false;

    // Complaints about requested-but-unavailable measurements: once per item
    bool unavailableLogged[TDI_COUNT] = {};

    while (true)
    {
        task_heartbeat();

        int interval_s = DataStore::getInstance().get_value(CFG_INTERVAL, DEFAULT_INTERVAL_S);
        if (interval_s < 5) interval_s = DEFAULT_INTERVAL_S;

        float temp_offset = DataStore::getInstance().get_value(CFG_TEMP_OFFSET, 0.0f);

        int decimals = DataStore::getInstance().get_value<int>(CFG_DECIMALS, 1);
        if (decimals < 0 || decimals > 1) decimals = 1;

        // Display cadence, decoupled from the poll interval: 0 = every wake
        int show_interval_s = DataStore::getInstance().get_value<int>(CFG_SHOW_INTERVAL, 0);
        if (show_interval_s < 0) show_interval_s = 0;

        // How long each reading stays on screen: 0 = the historical ~2 s flash
        int hold_s = DataStore::getInstance().get_value<int>(CFG_HOLD, 0);
        if (hold_s < 0) hold_s = 0;
        if (hold_s > 30) hold_s = 30;
        int holdMs = hold_s * 1000;

        // What to show, in what order; empty list = never scroll
        std::vector<TempDisplayItem> items = parseDisplayList(
            DataStore::getInstance().get_value(CFG_DISPLAY, kTempDisplayDefault));

        // Drop measurements this sensor cannot provide (log once per item)
        for (auto it = items.begin(); it != items.end();)
        {
            if (!itemAvailable(*it, sensor->hasHumidity(), sensor->hasPressure()))
            {
                if (!unavailableLogged[*it])
                {
                    unavailableLogged[*it] = true;
                    logPrintf("TMP", "'%s' requested but sensor '%s' does not provide it",
                              tempDisplayName(*it), sensor->name());
                }
                it = items.erase(it);
            }
            else
            {
                ++it;
            }
        }

        time_t now = time(nullptr);

        // Refresh reading when interval has elapsed
        if (difftime(now, last_update) >= interval_s)
        {
            read_success = sensor->read();
            if (read_success)
            {
                last_update = now;
                auto& rs = RuntimeStore::getInstance();
                temp = sensor->temperature() + temp_offset;
                rs.set("temp_c", temp, runtimeTempFormat(decimals));
                if (sensor->hasPressure())
                {
                    pressure = sensor->pressure();
                    rs.set("temp_hpa", pressure, "%.0f");

                    // Feed the trend history and refresh the trend placeholders
                    PressureHistoryStore::getInstance().add(now, pressure);
                    haveTrend = PressureHistoryStore::getInstance().trend(now, trendKind, trendRate);
                    if (haveTrend)
                    {
                        rs.set("pressure_trend", pressureTrendName(trendKind));
                        rs.set("pressure_rate", trendRate, "%+.2f");
                    }
                }

                if (sensor->hasHumidity())
                {
                    humidity = sensor->humidity();
                    rs.set("temp_rh", humidity, "%.0f");
                }
            }
        }

        if (not read_success)
        {
            logPrintf("TMP", "failed to read from sensor '%s'", sensor->name());
            vTaskDelay(30000 / portTICK_PERIOD_MS);
            continue;
        }

        bool showDue = !items.empty() &&
                       (show_interval_s == 0 || difftime(now, last_display) >= show_interval_s);
        if (showDue)
        {
            // Blocking acquire: cover the worst-case wait + scroll holds, then
            // re-anchor the watchdog window once granted (clock-task pattern).
            task_heartbeat_grace(90000);
            if (auto display = rmd.acquire())
            {
                task_heartbeat();
                task_heartbeat_grace(30000);
                for (size_t i = 0; i < items.size(); i++)
                {
                    switch (items[i])
                    {
                        case TDI_TEMP:
                            showMessage(formatTempLine(temp, decimals), display, holdMs);
                            break;
                        case TDI_HUMIDITY:
                            showMessage(formatHumidityLine(humidity), display, holdMs);
                            break;
                        case TDI_PRESSURE:
                        {
                            const uint8_t* trendIcon = haveTrend ? trendIconFor(trendKind) : nullptr;
                            showMessage(trendIcon, kWeatherIconWidth,
                                        formatPressureLine(pressure), display, holdMs);
                            break;
                        }
                        default:
                            break;
                    }
                }
                last_display = time(nullptr);
            }
        }

        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}
