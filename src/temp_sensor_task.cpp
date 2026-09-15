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
#include <weather_icons.hpp>
#include <string>

// Default display update interval in seconds
static constexpr int DEFAULT_INTERVAL_S = 300;

// Config keys
static const char CFG_INTERVAL[] = "temp_interval";
static const char CFG_TEMP_OFFSET[] = "temp_offset";

// Pressure trend arrow glyph for the pressure readout
static const uint8_t* trendIconFor(TrendKind kind)
{
    switch (kind)
    {
        case TREND_RISING_FAST:  return kWeatherIcons[WI_ARROW_UP_FAST];
        case TREND_RISING:       return kWeatherIcons[WI_ARROW_UP];
        case TREND_FALLING:      return kWeatherIcons[WI_ARROW_DOWN];
        case TREND_FALLING_FAST: return kWeatherIcons[WI_ARROW_DOWN_FAST];
        default:                 return kWeatherIcons[WI_ARROW_STEADY];
    }
}

void temp_sensor_task(void* parameter)
{
    registerTask("TempSensor", 4096);
    TempSensor* sensor = static_cast<TempSensor*>(parameter);

    logPrintf("TMP", "starting with sensor '%s'", sensor->name());

    if (!sensor->begin())
    {
        logPrintf("TMP", "sensor '%s' failed to initialize, task exiting", sensor->name());
        vTaskDelete(nullptr);
        return;
    }

    auto& rmd = ResourceManager<LMDS>::getInstance();
    time_t last_update = 0;

    float temp = 0.0f;
    float pressure = 0.0f;
    float humidity = 0.0f;

    // Pressure trend (valid only when the sensor provides pressure)
    bool haveTrend = false;
    TrendKind trendKind = TREND_STEADY;
    float trendRate = 0.0f;

    bool read_success = false;

    while (true)
    {
        int interval_s = DataStore::getInstance().get_value(CFG_INTERVAL, DEFAULT_INTERVAL_S);
        if (interval_s < 5) interval_s = DEFAULT_INTERVAL_S;

        float temp_offset = DataStore::getInstance().get_value(CFG_TEMP_OFFSET, 0.0f);
        

        // Refresh reading when interval has elapsed
        if (difftime(time(nullptr), last_update) >= interval_s)
        {
            read_success = sensor->read();
            if (read_success)
            {                 
                last_update = time(nullptr);                
                auto& rs = RuntimeStore::getInstance();
                temp = sensor->temperature() + temp_offset;
                rs.set("temp_c", temp, "%.1f\xC2\xB0" "C");
                if (sensor->hasPressure())
                {
                    pressure = sensor->pressure();
                    rs.set("temp_hpa", pressure, "%.0f");

                    // Feed the trend history and refresh the trend placeholders
                    PressureHistoryStore::getInstance().add(time(nullptr), pressure);
                    haveTrend = PressureHistoryStore::getInstance().trend(time(nullptr), trendKind, trendRate);
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

        char buffer[16];
        if (auto display = rmd.acquire())
        {
            snprintf(buffer, sizeof(buffer), "%.1f\xF7" "C", temp);
            scrollMessage(buffer, display, 20);

            if (sensor->hasPressure())
            {
                snprintf(buffer, sizeof(buffer), "%.0f hPa", pressure);
                const uint8_t* trendIcon = haveTrend ? trendIconFor(trendKind) : nullptr;
                scrollMessage(trendIcon, kWeatherIconWidth, buffer, display, 20);
            }
        }
        
        vTaskDelay(30000 / portTICK_PERIOD_MS);
        
        
    }
}
