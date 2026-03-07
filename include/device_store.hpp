#pragma once
#include <string>
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <version.hpp>

// Read-only store that provides live device parameters via the same get(key)
// interface as RuntimeStore / DataStore.  All writes are silently ignored.
// No mutex is needed: nothing is mutable after construction.
//
// Supported keys:
//   ip       – current IP address
//   hostname – mDNS / DHCP hostname
//   ssid     – connected WiFi network name
//   rssi     – signal strength in dBm
//   mac      – WiFi MAC address
//   gateway  – default gateway IP
//   channel  – WiFi channel number
//   heap     – free heap in bytes
//   min_heap – minimum free heap since boot
//   uptime   – uptime as "Xh YYm"
//   chip     – chip model string
//   cpu_freq – CPU frequency in MHz
//   sdk      – IDF / SDK version string
//   tasks    – number of running FreeRTOS tasks
//   version  – firmware version (APP_VERSION)
//   build    – build date (BUILD_DATE)
class DeviceStore
{
public:
    static DeviceStore& getInstance()
    {
        static DeviceStore instance;
        return instance;
    }

    DeviceStore(const DeviceStore&) = delete;
    DeviceStore& operator=(const DeviceStore&) = delete;

    std::string get(const std::string& key, const std::string& default_value = "") const
    {
        char buf[32];

        // --- compile-time constants (no buf needed) ---
        if (key == "version") return APP_VERSION;
        if (key == "build")   return BUILD_DATE;

        // --- string APIs ---
        if (key == "ip")       return WiFi.localIP().toString().c_str();
        if (key == "hostname") return WiFi.getHostname();
        if (key == "ssid")     return WiFi.SSID().c_str();
        if (key == "mac")      return WiFi.macAddress().c_str();
        if (key == "gateway")  return WiFi.gatewayIP().toString().c_str();
        if (key == "chip")     return ESP.getChipModel();
        if (key == "sdk")      return ESP.getSdkVersion();

        // --- numeric APIs ---
        if (key == "rssi")
        {
            snprintf(buf, sizeof(buf), "%d", WiFi.RSSI());
            return buf;
        }
        if (key == "channel")
        {
            snprintf(buf, sizeof(buf), "%d", WiFi.channel());
            return buf;
        }
        if (key == "heap")
        {
            snprintf(buf, sizeof(buf), "%u", (unsigned)esp_get_free_heap_size());
            return buf;
        }
        if (key == "min_heap")
        {
            snprintf(buf, sizeof(buf), "%u", (unsigned)esp_get_minimum_free_heap_size());
            return buf;
        }
        if (key == "cpu_freq")
        {
            snprintf(buf, sizeof(buf), "%u MHz", (unsigned)ESP.getCpuFreqMHz());
            return buf;
        }
        if (key == "tasks")
        {
            snprintf(buf, sizeof(buf), "%u", (unsigned)uxTaskGetNumberOfTasks());
            return buf;
        }
        if (key == "uptime")
        {
            unsigned long ms = millis();
            snprintf(buf, sizeof(buf), "%luh%02lum",
                     ms / 3600000UL, (ms / 60000UL) % 60UL);
            return buf;
        }

        return default_value;
    }

    bool has(const std::string& key) const
    {
        static const char* const keys[] = {
            "ip", "hostname", "ssid", "mac", "gateway", "channel",
            "rssi", "heap", "min_heap", "uptime",
            "chip", "cpu_freq", "sdk", "tasks",
            "version", "build"
        };
        for (auto k : keys)
            if (key == k) return true;
        return false;
    }

    // Writes are intentionally ignored.
    void set(const std::string&, const std::string&) {}

private:
    DeviceStore() = default;
};
