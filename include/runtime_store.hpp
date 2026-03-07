#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <map>
#include <string>
#include <cstdio>

// Volatile key-value store for runtime state (sensor readings, computed values, …).
// All entries are lost on reboot — nothing is written to flash.
// Multiple FreeRTOS tasks may read and write concurrently; a mutex protects the map.
//
// Usage:
//   RuntimeStore::getInstance().set("temp_c", 22.5f);          // float convenience
//   RuntimeStore::getInstance().set("beam_mode", "STABLE");    // string
//   std::string t = RuntimeStore::getInstance().get("temp_c");  // read
//
// {key} placeholders in custom messages check RuntimeStore first, then DataStore.
class RuntimeStore
{
public:
    static RuntimeStore& getInstance()
    {
        static RuntimeStore instance;
        return instance;
    }

    RuntimeStore(const RuntimeStore&) = delete;
    RuntimeStore& operator=(const RuntimeStore&) = delete;

    // Store a string value.
    void set(const std::string& key, const std::string& value)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        data_[key] = value;
        xSemaphoreGive(mutex_);
    }

    // Convenience overload: format a float with a printf format string.
    void set(const std::string& key, float value, const char* fmt = "%.1f")
    {
        char buf[32];
        snprintf(buf, sizeof(buf), fmt, value);
        set(key, std::string(buf));
    }

    // Return the value for key, or default_value if absent.
    std::string get(const std::string& key, const std::string& default_value = "")
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        auto it = data_.find(key);
        std::string result = (it != data_.end()) ? it->second : default_value;
        xSemaphoreGive(mutex_);
        return result;
    }

    bool has(const std::string& key)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        bool found = data_.count(key) > 0;
        xSemaphoreGive(mutex_);
        return found;
    }

    void remove(const std::string& key)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        data_.erase(key);
        xSemaphoreGive(mutex_);
    }

    // Return a point-in-time copy of all entries (e.g., for web display).
    std::map<std::string, std::string> snapshot()
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        auto copy = data_;
        xSemaphoreGive(mutex_);
        return copy;
    }

private:
    RuntimeStore() { mutex_ = xSemaphoreCreateMutex(); }
    ~RuntimeStore()
    {
        if (mutex_) vSemaphoreDelete(mutex_);
    }

    std::map<std::string, std::string> data_;
    SemaphoreHandle_t mutex_ = nullptr;
};
