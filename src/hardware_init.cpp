#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <data_store.hpp>
#include <logger.hpp>
#include <task_registry.hpp>

static volatile bool s_ap_mode = false;   // true while not connected to any AP
static std::string s_hostname = "infoclock32";

bool wifi_is_ap_mode() { return s_ap_mode; }

static void start_mdns()
{
    if (MDNS.begin(s_hostname.c_str()))
        logPrintf("WIFI", "mDNS started — http://%s.local", s_hostname.c_str());
    MDNS.addService("http", "tcp", 80);
}

// Persistent watchdog. Survives the WiFiManager portal closing: whenever the
// STA link drops (or boot ended offline), re-arms the connection with the last
// credentials WiFi.begin()/WiFiManager used and keeps retrying every 30 s.
static void wifi_monitor_task(void* /*pv*/)
{
    registerTask("WiFiMonitor", 4096, 90000);
    bool wasConnected = false;
    for (;;)
    {
        task_heartbeat();
        if (WiFi.status() == WL_CONNECTED)
        {
            if (!wasConnected)
            {
                wasConnected = true;
                s_ap_mode = false;
                logPrintf("WIFI", "connected, IP=%s",
                          WiFi.localIP().toString().c_str());
                start_mdns();
            }
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if (wasConnected)
            logPrintf("WIFI", "link lost — retrying every 30 s");
        wasConnected = false;
        s_ap_mode = true;
        WiFi.reconnect();   // no-op if the device never had credentials
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}

void hardware_init()
{
    Serial.println("hardware_init: start");

    auto& ds = DataStore::getInstance();
    s_hostname = ds.get_value("hostname", "infoclock32");
    std::string ssid     = ds.get_value("wifi_ssid", "");
    std::string password = ds.get_value("wifi_password", "");

    int portal_timeout_s = max(30, min(600,
        ds.get_value<int>("wifi_portal_timeout_s", 180)));

    WiFi.setHostname(s_hostname.c_str());

    std::string apName = s_hostname + "-setup";

    // config.txt is the single source of truth for station credentials.
    // The setup portal is just another editor of the file: whatever is
    // provisioned there is mirrored into wifi_ssid/wifi_password, so the
    // /wifi page, /edit and the portal all write the same keys and a
    // provisioned device boots straight from the file.
    static WiFiManager wm;
    wm.setHostname(s_hostname.c_str());
    wm.setConfigPortalTimeout(portal_timeout_s);
    wm.setBreakAfterConfig(true);
    wm.setSaveConfigCallback([&wm]() {
        auto& ds = DataStore::getInstance();
        ds.set_value("wifi_ssid",     std::string(wm.getWiFiSSID().c_str()));
        ds.set_value("wifi_password", std::string(wm.getWiFiPass().c_str()));
        ds.save_to_file("/config.txt");
        logPrintf("WIFI", "portal credentials saved to config, SSID=%s",
                  wm.getWiFiSSID().c_str());
    });

    bool connected = false;
    if (!ssid.empty())
    {
        // Try the config.txt credentials first — without persisting them, so
        // the attempt cannot rewrite the NVS cache (issue #3).
        WiFi.persistent(false);
        logPrintf("WIFI", "trying config.txt credentials, SSID=%s", ssid.c_str());
        WiFi.begin(ssid.c_str(), password.c_str());
        for (int i = 0; i < 100; i++)
        {
            wl_status_t st = WiFi.status();
            if (st == WL_CONNECTED ||
                st == WL_NO_SSID_AVAIL ||     // SSID not in the air
                st == WL_CONNECT_FAILED)      // rejected by the AP
                break;
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        connected = WiFi.status() == WL_CONNECTED;
        logPrintf("WIFI", connected ? "connected with config.txt credentials"
                                    : "config.txt credentials failed");
    }

    if (!connected)
    {
        if (!ssid.empty())
        {
            // The file is the authority and it failed: erase the stored pair
            // so a stale cache cannot outvote it, and autoConnect() opens the
            // portal directly (no doomed stored-credentials attempt first).
            WiFi.disconnect(false, true);
            logPrintf("WIFI", "opening setup portal at %s / 192.168.4.1",
                      apName.c_str());
        }
        // With wifi_ssid blank this still tries the NVS pair first — the
        // recovery hatch for a freshly reflashed filesystem.
        connected = wm.autoConnect(apName.c_str());
    }

    if (connected)
    {
        s_ap_mode = false;
        logPrintf("WIFI", "connected, IP=%s",
                  WiFi.localIP().toString().c_str());
        start_mdns();
    }
    else
    {
        s_ap_mode = true;
        logPrintf("WIFI", "offline — reboot to reopen setup portal at %s / 192.168.4.1",
                  apName.c_str());
    }

    xTaskCreate(wifi_monitor_task, "WiFiMonitor", 4096, nullptr, 1, nullptr);
}
