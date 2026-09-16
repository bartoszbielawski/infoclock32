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

    // Standard WiFiManager provisioning (2.0.x): autoConnect() uses the
    // NVS-stored station credentials, and opens a captive portal
    // ("<hostname>-setup" @ 192.168.4.1) when they fail or are absent.
    // The portal gives up after wifi_portal_timeout_s so the clock still
    // boots offline; rebooting reopens it.
    static WiFiManager wm;
    wm.setHostname(s_hostname.c_str());
    wm.setConfigPortalTimeout(portal_timeout_s);
    wm.setBreakAfterConfig(true);

    if (!ssid.empty())
        WiFi.begin(ssid.c_str(), password.c_str());  // config.txt wins: seeds WM's credential store

    std::string apName = s_hostname + "-setup";
    bool connected = wm.autoConnect(apName.c_str());

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
