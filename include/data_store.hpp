#pragma once
#ifndef INFOCLOCK32_INCLUDE_DATA_STORE_HPP
#define INFOCLOCK32_INCLUDE_DATA_STORE_HPP

#include <map>
#include <string>
#include <vector>
#include <type_traits>
#include <parse_utils.hpp>
#include <LittleFS.h>
#include <Arduino.h>

class DataStore
{
public:
    static DataStore& getInstance()
    {
        static DataStore instance;
        return instance;
    }

    // Delete copy constructor and assignment operator to prevent copies
    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;

    void load_from_file(const std::string& filename)
    {
        LittleFS.begin(true);

        File file = LittleFS.open(filename.c_str(), "r");
        if (!file) {
            Serial.println("Failed to open file for reading");
            return;
        }

        auto size = file.size();

        
        while (file.position() < size)
        {
            char buffer[128];
            size_t bytesRead = file.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
            buffer[bytesRead] = '\0';
            std::string line(buffer);
            // split into key and value separated by '=' and store in a map
            if (line.empty())
                continue;
            if (line[0] == '#') // skip comments
                continue;

            auto delimiterPos = line.find('=');
            if (delimiterPos == std::string::npos)
                continue;

            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);
            data[key] = value;
            Serial.printf("Loaded key: %s, value: %s\n", key.c_str(), value.c_str());
        }
        file.close();
        LittleFS.end();
    }
   
    void set_value(const std::string& key, const std::string& value)
    {
        data[key] = value;
    }

    void remove_value(const std::string& key)
    {
        data.erase(key);
    }

    std::vector<std::string> get_keys_with_prefix(const std::string& prefix) const
    {
        std::vector<std::string> keys;
        for (const auto& kv : data)
            if (kv.first.size() >= prefix.size() &&
                kv.first.compare(0, prefix.size(), prefix) == 0)
                keys.push_back(kv.first);
        return keys;
    }

    void save_to_file(const std::string& filename)
    {
        LittleFS.begin(true);
        File file = LittleFS.open(filename.c_str(), "w");
        if (!file) return;
        for (const auto& kv : data)
            file.printf("%s=%s\n", kv.first.c_str(), kv.second.c_str());
        file.close();
        LittleFS.end();
    }

    // Typed overload: delegates to parse_value(). Supported: int, long, float.
    // Returns default_value on missing key or parse failure.
    // enable_if prevents ambiguity with the string overload when called with string literals.
    template<typename T, typename std::enable_if<
        std::is_same<T, int>::value || std::is_same<T, long>::value || std::is_same<T, float>::value,
        int>::type = 0>
    T get_value(const std::string& key, T default_value) const
    {
        auto it = data.find(key);
        if (it == data.end()) return default_value;
        return parse_value(it->second, default_value);
    }

    std::string get_value(const std::string& key, const std::string& default_value = "")
    {
        auto it = data.find(key);
        if (it != data.end())
        {
            return it->second;
        }
        return default_value;
    }

    bool has_key(const std::string& key)
    {
        return data.find(key) != data.end();
    }

    void clear()
    {
        data.clear();
    }

private:
    DataStore()
    {
        // load data from persistent storage if needed
        // from SPIFFS   
    }
    ~DataStore() = default;

    std::map<std::string, std::string> data;
};
#endif // INFOCLOCK32_INCLUDE_DATA_STORE_HPP