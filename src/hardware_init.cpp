#include <Arduino.h>
#include <WiFiManager.h>

void hardware_init()
{
    Serial.begin(1000000);
    Serial.println("Start");
    WiFiManager wifiManager;

    WiFiManagerParameter display_segments("display_segments", "Display Segments", "8", 2);

    wifiManager.addParameter(&display_segments);

    display_segments.getValue();

    Serial.println("Connecting to WiFi...");
    bool result = wifiManager.autoConnect();
    Serial.println(result ? "Connected" : "Not connected");
}