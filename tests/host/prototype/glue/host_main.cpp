// Host prototype entry point: replaces setup()/loop() without touching any
// firmware source. Runs 11 real firmware tasks as threads.
#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFiClient.h>
#include <esp_system.h>

#include <cmath>
#include <csignal>
#include <cstring>
#include <thread>

#include <data_store.hpp>
#include <logger.hpp>
#include <timezone_utils.hpp>
#include <task_entry_points.hpp>
#include <task_registry.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <screen_wipe_task.h>
#include <temp_sensor.hpp>
#include <watchdog_task.h>

#include "display.hpp"

static const int HOST_CS_PIN = 7;

static void boot_banner_task(void*)
{
    auto& rmd = ResourceManager<LMDS>::getInstance();
    if (auto display = rmd.acquire())
    {
        scrollMessage("infoclock32 host prototype", display, 30);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(nullptr);
}

static void printUsage()
{
    fprintf(stderr,
            "usage: prototype [--offline] [--static] [--hang <TaskName>] [--fs <dir>]\n"
            "  --offline        canned deterministic API responses, no MQTT task\n"
            "  --static         plain text frames instead of ANSI live view\n"
            "  --hang <Task>    stop heartbeats of <Task> after 20 s (watchdog demo)\n"
            "  --fs <dir>       filesystem root for /config.txt (default: ./fs)\n");
}

int main(int argc, char** argv)
{
    bool offline = false;
    bool staticText = false;
    const char* fsDir = "fs";
    const char* hangName = nullptr;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--offline")) offline = true;
        else if (!strcmp(argv[i], "--static")) staticText = true;
        else if (!strcmp(argv[i], "--hang") && i + 1 < argc) hangName = argv[++i];
        else if (!strcmp(argv[i], "--fs") && i + 1 < argc) fsDir = argv[++i];
        else { printUsage(); return 1; }
    }

    LittleFS.setRoot(fsDir);
    LittleFS.begin(true);
    DataStore::getInstance().load_from_file("/config.txt");
    logger_init();
    apply_timezone();

    if (offline) host_set_http_transport(host_http_canned);

    auto* lmds = new LMDS(SPI, SPISettings(5000000, MSBFIRST, SPI_MODE0), 8, HOST_CS_PIN);
    lmds->begin();
    host_display::init(lmds, !staticText);
    auto& rmd = ResourceManager<LMDS>::getInstance();
    rmd.initialize(lmds);
    rmd.setPreReleaseHook(wipe_on_release);

    int brightness = DataStore::getInstance().get_value<int>("brightness", 7);
    brightness = max(0, min(15, brightness));
    lmds->setIntensity((uint8_t)brightness);

    // Device-faithful boot: show a banner before the tasks start contending
    // for the display (main.cpp does the same on hardware). Must run in a
    // real task — the ResourceManager handshake only works from task threads
    // (on the device, setup() itself runs in loopTask).
    xTaskCreate(boot_banner_task, "BootBanner", 4096, nullptr, 1, nullptr);
    vTaskDelay(11000 / portTICK_PERIOD_MS);   // banner scroll + settle delay

    logPrintf("SYS", "host prototype starting%s", offline ? " (offline)" : "");

    xTaskCreate(displayClock, "ClockTask", 4096, nullptr, 1, nullptr);
    xTaskCreate(open_weather_map_task, "WeatherTask", 8192, nullptr, 1, nullptr);
    xTaskCreate(lhc_status_task, "LHCStatusTask", 8192, nullptr, 1, nullptr);
    if (!offline)
        xTaskCreate(mqtt_task, "MQTTTask", 8192, nullptr, 1, nullptr);
    else
        logPrintf("SYS", "MQTTTask skipped (--offline)");

    TempSensor* sensor = new StubTempSensor();
    xTaskCreate(temp_sensor_task, "TempSensorTask", 4096, sensor, 1, nullptr);
    xTaskCreate(custom_message_task, "CustomMessageTask", 4096, nullptr, 1, nullptr);
    xTaskCreate(night_mode_task, "NightModeTask", 2048, nullptr, 1, nullptr);
    xTaskCreate(resto_menu_task, "RestoMenuTask", 8192, nullptr, 1, nullptr);

    float sunLat = DataStore::getInstance().get_value<float>("sun_lat", NAN);
    float sunLon = DataStore::getInstance().get_value<float>("sun_lon", NAN);
    if (isfinite(sunLat) && isfinite(sunLon))
        xTaskCreate(sun_times_task, "SunTimesTask", 4096, nullptr, 1, nullptr);
    else
        logPrintf("SYS", "SunTimesTask not started (sun_lat/sun_lon not set)");

    // Match the device: LifeTask is gated on enable_life in main.cpp.
    if (DataStore::getInstance().get_value<int>("enable_life", 0))
        xTaskCreate(game_of_life_task, "LifeTask", 4096, nullptr, 1, nullptr);
    xTaskCreate(web_server_task, "WebServerTask", 10240, nullptr, 1, nullptr);
    start_watchdog_task();

    if (hangName)
    {
        host_set_hang(hangName);
        logPrintf("SYS", "hang injection armed for '%s' (fires 20 s after boot)", hangName);
    }

    // The main thread just idles; SIGINT terminates the process.
    for (;;)
        std::this_thread::sleep_for(std::chrono::hours(24));
}
