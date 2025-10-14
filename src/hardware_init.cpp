#include <Arduino.h>
#include <WiFiManager.h>

void hardware_init()
{
    Serial.begin(1000000);
    Serial.println("Start");
    WiFiManager wifiManager;
    Serial.println("Connecting to WiFi...");
    bool result = wifiManager.autoConnect();
    Serial.println(result ? "Connected" : "Not connected");
}