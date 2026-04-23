#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include <data_store.hpp>
#include <logger.hpp>
#include <task_registry.hpp>

void ota_task(void*)
{
    registerTask("OTA");
    while (WiFi.status() != WL_CONNECTED)
        vTaskDelay(1000 / portTICK_PERIOD_MS);

    DataStore& ds = DataStore::getInstance();
    std::string hostname = ds.get_value("hostname", "infoclock32");
    std::string password = ds.get_value("ota_password", "");

    ArduinoOTA.setHostname(hostname.c_str());
    if (!password.empty())
        ArduinoOTA.setPassword(password.c_str());

    ArduinoOTA.onStart([]() {
        logPrintf("OTA", "update started");
    });
    ArduinoOTA.onEnd([]() {
        logPrintf("OTA", "update complete — rebooting");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        logPrintf("OTA", "error %u", error);
    });

    ArduinoOTA.begin();
    logPrintf("OTA", "ready on %s.local", hostname.c_str());

    while (true)
    {
        ArduinoOTA.handle();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
