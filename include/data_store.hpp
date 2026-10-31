#pragma once
#ifndef INFOCLOCK32_INCLUDE_DATA_STORE_HPP
#define INFOCLOCK32_INCLUDE_DATA_STORE_HPP

#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <type_traits>
#include <parse_utils.hpp>
#include <LittleFS.h>
#include <secret_keys.hpp>
#include <config_utils.hpp>
#include <config_parse.hpp>
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

    // Replaces the whole in-memory config with the file's content, so keys
    // removed from the file disappear too. The file is read and parsed first;
    // if it cannot be opened or read completely the current config is kept
    // and false is returned.
    bool load_from_file(const std::string& filename)
    {
        File file = LittleFS.open(filename.c_str(), "r");
        if (!file) {
            Serial.println("Failed to open file for reading");
            return false;
        }

        const size_t size = file.size();
        if (size > MAX_CONFIG_BYTES) {
            Serial.printf("Config file too large (%u bytes), keeping current settings\n", (unsigned)size);
            file.close();
            return false;
        }
        std::string text;
        text.reserve(size);
        while (text.size() < size && file.available())
            text.push_back((char)file.read());
        file.close();
        if (text.size() != size) {
            Serial.println("Config read incomplete, keeping current settings");
            return false;
        }

        std::vector<std::string> tooLong;
        std::map<std::string, std::string> parsed = parseConfigText(text, &tooLong);
        for (const auto& l : tooLong)
            Serial.printf("Config line too long (>%u chars), ignored: %.24s...\n",
                          (unsigned)CONFIG_MAX_LINE, l.c_str());
        for (const auto& kv : parsed)
            Serial.printf("Loaded key: %s, value: %s\n", kv.first.c_str(),
                          loggableValue(kv.first, kv.second));

        std::lock_guard<std::mutex> lock(data_mutex_);
        data.swap(parsed);
        return true;
    }

    void set_value(const std::string& key, const std::string& value)
    {
        // Keys that cannot round-trip through config.txt are refused, and line
        // breaks in values are folded to spaces (see config_utils.hpp).
        if (!isValidConfigKey(key)) return;
        std::string clean = sanitizeConfigValue(value);
        std::lock_guard<std::mutex> lock(data_mutex_);
        data[key] = clean;
    }

    void remove_value(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        data.erase(key);
    }

    std::vector<std::string> get_keys_with_prefix(const std::string& prefix) const
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        std::vector<std::string> keys;
        for (const auto& kv : data)
            if (kv.first.size() >= prefix.size() &&
                kv.first.compare(0, prefix.size(), prefix) == 0)
                keys.push_back(kv.first);
        return keys;
    }

    void save_to_file(const std::string& filename)
    {
        // Snapshot under data_mutex_, then do the filesystem work without it
        // so get_value() readers never block on flash I/O. save_mutex_
        // serializes concurrent saves (WebServerTask and the MQTT task can
        // both end up here, and they share the same temp file).
        std::map<std::string, std::string> snap;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            snap = data;
        }

        std::lock_guard<std::mutex> saveLock(save_mutex_);

        // Read existing lines so comments and blank lines are preserved.
        std::vector<std::string> lines;
        File rf = LittleFS.open(filename.c_str(), "r");
        if (rf) {
            while (rf.available()) {
                String s = rf.readStringUntil('\n');
                while (s.length() > 0 &&
                       (s[s.length()-1] == '\r' || s[s.length()-1] == '\n'))
                    s.remove(s.length()-1);
                lines.push_back(std::string(s.c_str()));
            }
            rf.close();
        }

        // Track which keys still need to be appended after the existing lines.
        std::map<std::string, bool> emitted;
        for (const auto& kv : snap)
            emitted[kv.first] = false;

        // Write to a temp file and rename it over the target, so a power cut
        // mid-write leaves /config.txt intact (previous content) instead of
        // truncated. remove() is only a fallback in case the filesystem
        // refuses rename-over-existing.
        std::string tmp = filename + ".tmp";
        File wf = LittleFS.open(tmp.c_str(), "w");
        if (!wf) return;

        for (const auto& line : lines) {
            // Preserve blank lines and comments verbatim.
            if (line.empty() || line[0] == '#') {
                wf.printf("%s\n", line.c_str());
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) {
                wf.printf("%s\n", line.c_str());
                continue;
            }
            std::string key = line.substr(0, eq);
            auto it = snap.find(key);
            if (it != snap.end()) {
                wf.printf("%s=%s\n", key.c_str(), it->second.c_str());
                emitted[key] = true;
            }
            // Key removed from DataStore — omit the line.
        }

        // Append any keys that were not present in the original file.
        for (const auto& kv : snap)
            if (!emitted[kv.first])
                wf.printf("%s=%s\n", kv.first.c_str(), kv.second.c_str());

        wf.close();

        if (!LittleFS.rename(tmp.c_str(), filename.c_str()))
        {
            LittleFS.remove(filename.c_str());
            if (!LittleFS.rename(tmp.c_str(), filename.c_str()))
                Serial.println("[DS] config save failed: rename to target lost");
        }
    }

    // Typed overload: delegates to parse_value(). Supported: int, long, float.
    // Returns default_value on missing key or parse failure.
    // enable_if prevents ambiguity with the string overload when called with string literals.
    template<typename T, typename std::enable_if<
        std::is_same<T, int>::value || std::is_same<T, long>::value || std::is_same<T, float>::value,
        int>::type = 0>
    T get_value(const std::string& key, T default_value) const
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto it = data.find(key);
        if (it == data.end()) return default_value;
        return parse_value(it->second, default_value);
    }

    std::string get_value(const std::string& key, const std::string& default_value = "")
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto it = data.find(key);
        if (it != data.end())
        {
            return it->second;
        }
        return default_value;
    }

    bool has_key(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return data.find(key) != data.end();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        data.clear();
    }

private:
    static constexpr size_t MAX_CONFIG_BYTES = 32768;

    DataStore()
    {
        // load data from persistent storage if needed
        // from SPIFFS   
    }
    ~DataStore() = default;

    mutable std::mutex data_mutex_;   // guards `data`
    std::mutex save_mutex_;           // serializes save_to_file I/O
    std::map<std::string, std::string> data;
};
#endif // INFOCLOCK32_INCLUDE_DATA_STORE_HPP