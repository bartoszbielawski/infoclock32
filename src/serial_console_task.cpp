#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <data_store.hpp>
#include <logger.hpp>
#include <secret_keys.hpp>
#include <serial_cmd.hpp>
#include <task_registry.hpp>
#include <serial_console_task.h>
#include <wifi_cache.h>

#include <string>

static void handle_command(const SerialCommand& c)
{
    auto& ds = DataStore::getInstance();
    switch (c.cmd)
    {
    case SerialCmd::None:
        break;
    case SerialCmd::Set:
        ds.set_value(c.key, c.value);
        ds.save_to_file("/config.txt");
        logPrintf("CON", "set %s=%s", c.key.c_str(), loggableValue(c.key, c.value));
        Serial.println("OK");
        break;
    case SerialCmd::Get:
        // Secrets are write-only: never echoed back.
        if (!ds.has_key(c.key))
            Serial.println("(unset)");
        else
            Serial.println(isSecretKey(c.key)
                ? "***"
                : ds.get_value(c.key, "").c_str());
        break;
    case SerialCmd::WifiForget:
        ds.set_value("wifi_ssid", "");
        ds.set_value("wifi_password", "");
        ds.save_to_file("/config.txt");
        wifi_erase_cache();
        logPrintf("CON", "wifi credentials erased");
        Serial.println("OK");
        break;
    case SerialCmd::WifiScan:
    {
        int n = WiFi.scanNetworks();
        for (int i = 0; i < n; i++)
            Serial.printf("%s ch%d %ddBm\n", WiFi.SSID(i).c_str(),
                          (int)WiFi.channel(i), (int)WiFi.RSSI(i));
        Serial.printf("%d network(s)\n", n);
        WiFi.scanDelete();
        break;
    }
    case SerialCmd::WifiCache:
    {
#ifndef INFOCLOCK_TEST_HOOKS
        // Test hook only: it re-enables storing Wi-Fi credentials in flash
        // (NVS), which normal firmware deliberately avoids.
        Serial.println("ERR wifi_cache is only available in test builds");
        break;
#else
        // Test hook: store a pair in the NVS cache the way old firmware did,
        // without touching config.txt.
        WiFi.persistent(true);
        WiFi.mode(WIFI_STA);
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);   // boot code left it on RAM
        WiFi.begin(c.key.c_str(), c.value.c_str());
        for (int i = 0; i < 100 && WiFi.status() != WL_CONNECTED; i++) delay(100);
        WiFi.persistent(false);
        logPrintf("CON", "wifi cached for %s, status %d", c.key.c_str(), (int)WiFi.status());
        break;
#endif
    }
    case SerialCmd::Reboot:
        logPrintf("CON", "reboot requested");
        Serial.flush();
        delay(200);
        ESP.restart();
        break;
    case SerialCmd::Help:
        Serial.println("set key=value | get key | wifi_scan | wifi_forget | reboot | help");
        break;
    case SerialCmd::Unknown:
        Serial.println("ERR unknown command (try: help)");
        break;
    }
}

void serial_console_task(void* /*pv*/)
{
    registerTask("SerialConsole", 4096);
    SerialLineBuffer buffer(256);
    std::string line;
    for (;;)
    {
        task_heartbeat();
        while (Serial.available() > 0)
        {
            switch (buffer.feed((char)Serial.read(), line))
            {
            case SerialLineBuffer::Result::Line:
                handle_command(parseSerialCommand(line));
                break;
            case SerialLineBuffer::Result::Overflow:
                Serial.println("ERR line too long (max 256 characters), ignored");
                break;
            default:
                break;
            }
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
