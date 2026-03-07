#include <Arduino.h>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <runtime_store.hpp>
#include <logger.hpp>
#include <temp_sensor.hpp>
#include <string>

// Default display update interval in seconds
static constexpr int DEFAULT_INTERVAL_S = 30;

// Config keys
static const char CFG_INTERVAL[] = "temp_interval";

// Build the message string from sensor readings.
// Format examples:
//   "Temperature: 22.5C 1013hPa"  (temp + pressure)
//   "Temperature: 22.5C"          (temp only)
static std::string buildMessage(TempSensor* sensor)
{
    char buf[64];

    if (sensor->hasPressure())
    {
        snprintf(buf, sizeof(buf), "Temperature: %.1fC %.0fhPa",
                 sensor->temperature(),
                 sensor->pressure());
    }
    else
    {
        snprintf(buf, sizeof(buf), "Temperature: %.1fC",
                 sensor->temperature());
    }

    return std::string(buf);
}

void temp_sensor_task(void* parameter)
{
    TempSensor* sensor = static_cast<TempSensor*>(parameter);

    logPrintf("TMP", "starting with sensor '%s'", sensor->name());

    if (!sensor->begin())
    {
        logPrintf("TMP", "sensor '%s' failed to initialize, task exiting", sensor->name());
        vTaskDelete(nullptr);
        return;
    }

    auto& rmd = ResourceManager<LMDS>::getInstance();
    std::string message;
    time_t last_update = 0;

    while (true)
    {
        int interval_s = DataStore::getInstance().get_value(CFG_INTERVAL, DEFAULT_INTERVAL_S);
        if (interval_s < 5) interval_s = DEFAULT_INTERVAL_S;

        // Refresh reading when interval has elapsed
        if (difftime(time(nullptr), last_update) >= interval_s)
        {
            if (sensor->read())
            {
                message = buildMessage(sensor);
                last_update = time(nullptr);
                logPrintf("TMP", "%s", message.c_str());

                // Publish to RuntimeStore so {temp_c} / {temp_hpa} / {temp_rh}
                // can be used in custom message placeholders.
                auto& rs = RuntimeStore::getInstance();
                rs.set("temp_c", sensor->temperature());
                if (sensor->hasPressure())  rs.set("temp_hpa", sensor->pressure(), "%.0f");
                if (sensor->hasHumidity())  rs.set("temp_rh",  sensor->humidity(), "%.0f");
            }
            else
            {
                logPrintf("TMP", "read() returned false, no data");
                message.clear();
                last_update = time(nullptr); // back off, don't hammer a failing sensor
            }
        }

        if (message.empty())
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if (!rmd.make_access_request())
        {
            logPrintf("TMP", "failed to get display access");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        auto& matrix = rmd.getResourceRef();
        scrollMessage(message, matrix, 50);
        rmd.release_access();

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
