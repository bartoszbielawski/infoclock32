#include <Arduino.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <data_store.hpp>

void hardware_init()
{
    // Serial is already initialised by setup() before this call.
    Serial.println("hardware_init: start");

    // Apply hostname before WiFi connects so DHCP and the captive-portal AP
    // name both reflect the configured value.
    std::string hostname = DataStore::getInstance().get_value("hostname", "infoclock32");
    WiFi.setHostname(hostname.c_str());

    WiFiManager wifiManager;

    WiFiManagerParameter display_segments("display_segments", "Display Segments", "8", 2);
    wifiManager.addParameter(&display_segments);
    display_segments.getValue();

    Serial.println("hardware_init: connecting to WiFi...");
    bool result = wifiManager.autoConnect(hostname.c_str());
    Serial.println(result ? "hardware_init: WiFi connected" : "hardware_init: WiFi not connected");

    // Start mDNS responder so the device is reachable as <hostname>.local
    if (MDNS.begin(hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("hardware_init: mDNS started — http://%s.local\n", hostname.c_str());
    } else {
        Serial.println("hardware_init: mDNS start failed");
    }
}
