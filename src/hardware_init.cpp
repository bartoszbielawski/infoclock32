#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <data_store.hpp>
#include <logger.hpp>

static bool s_ap_mode = false;
bool wifi_is_ap_mode() { return s_ap_mode; }

// Background task: waits for the STA side to connect (happens automatically
// once the router is reachable), then starts mDNS and exits.
// Only created when we enter AP+STA fallback mode.
static void wifi_reconnect_task(void* /*pv*/)
{
    while (WiFi.status() != WL_CONNECTED)
        vTaskDelay(5000 / portTICK_PERIOD_MS);

    s_ap_mode = false;
    logPrintf("SYS", "WiFi reconnected, IP=%s", WiFi.localIP().toString().c_str());

    std::string hostname = DataStore::getInstance().get_value("hostname", "infoclock32");
    if (MDNS.begin(hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        logPrintf("SYS", "mDNS started — http://%s.local", hostname.c_str());
    }

    vTaskDelete(nullptr);
}

void hardware_init()
{
    // Serial is already initialised by setup() before this call.
    Serial.println("hardware_init: start");

    auto& ds = DataStore::getInstance();
    std::string hostname = ds.get_value("hostname",     "infoclock32");
    std::string ssid     = ds.get_value("wifi_ssid",    "");
    std::string password = ds.get_value("wifi_password","");

    // Apply hostname before WiFi connects so DHCP reflects the configured value.
    WiFi.setHostname(hostname.c_str());

    if (!ssid.empty()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());
        Serial.println("hardware_init: connecting to WiFi...");

        // Wait up to 15 s (30 × 500 ms)
        int retries = 30;
        while (WiFi.status() != WL_CONNECTED && retries-- > 0)
            delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("hardware_init: WiFi connected, IP=%s\n",
                      WiFi.localIP().toString().c_str());
        if (MDNS.begin(hostname.c_str())) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("hardware_init: mDNS started — http://%s.local\n",
                          hostname.c_str());
        } else {
            Serial.println("hardware_init: mDNS start failed");
        }
        s_ap_mode = false;
    } else {
        // Fallback: AP+STA so the user can configure via /edit at 192.168.4.1
        // while the station side keeps retrying in the background.
        std::string apName = hostname + "-setup";
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(apName.c_str());
        if (!ssid.empty())
            WiFi.begin(ssid.c_str(), password.c_str());  // re-arm background retry
        Serial.printf("hardware_init: WiFi failed — AP '%s' started, "
                      "browse http://192.168.4.1/edit\n", apName.c_str());
        s_ap_mode = true;

        // Spawn monitor: starts mDNS and clears ap_mode flag when STA connects.
        xTaskCreate(wifi_reconnect_task, "WiFiReconnect", 2048, nullptr, 1, nullptr);
    }
}
