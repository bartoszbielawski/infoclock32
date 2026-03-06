#include <Arduino.h>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
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

    Serial.printf("TempSensor: starting with sensor '%s'\n", sensor->name());

    if (!sensor->begin())
    {
        Serial.printf("TempSensor: sensor '%s' failed to initialize, task exiting\n", sensor->name());
        vTaskDelete(nullptr);
        return;
    }

    auto& rmd = ResourceManager<LMDS>::getInstance();
    std::string message;
    time_t last_update = 0;

    while (true)
    {
        int interval_s = std::stoi(DataStore::getInstance().get_value(CFG_INTERVAL, "30"));
        if (interval_s < 5) interval_s = DEFAULT_INTERVAL_S;

        // Refresh reading when interval has elapsed
        if (difftime(time(nullptr), last_update) >= interval_s)
        {
            if (sensor->read())
            {
                message = buildMessage(sensor);
                last_update = time(nullptr);
                Serial.printf("TempSensor: %s\n", message.c_str());
            }
            else
            {
                Serial.printf("TempSensor: read() returned false, no data\n");
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
            Serial.println("TempSensor: failed to get display access");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        auto& matrix = rmd.getResourceRef();
        scrollMessage(message, matrix, 50);
        rmd.release_access();

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
