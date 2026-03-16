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
static constexpr int DEFAULT_INTERVAL_S = 300;

// Config keys
static const char CFG_INTERVAL[] = "temp_interval";
static const char CFG_TEMP_OFFSET[] = "temp_offset";

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
    time_t last_update = 0;

    float temp = 0.0f;
    float pressure = 0.0f;
    float humidity = 0.0f;

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
                rs.set("temp_c", temp);
                if (sensor->hasPressure())  
                {
                    pressure = sensor->pressure();
                    rs.set("temp_hpa", pressure, "%.0f");
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

        if (!rmd.make_access_request([](LMDS& m){ wipeStaticNoise(m); }))
        {
            logPrintf("TMP", "failed to get display access");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        auto& matrix = rmd.getResourceRef();
        //generate temperature message
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%.1f\xF7" "C", temp);        
        scrollMessage(buffer, matrix, 25);

        //generate pressure message if supported
        if (sensor->hasPressure())
        {
            snprintf(buffer, sizeof(buffer), "%.0f hPa", pressure);
            scrollMessage(buffer, matrix, 25);
        }   
        rmd.release_access();

        vTaskDelay(20000 / portTICK_PERIOD_MS);
    }
}
