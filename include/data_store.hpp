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

            // Strip trailing CR/LF so config files with Windows (CRLF) line
            // endings don't embed a '\r' into stored values.  readBytesUntil
            // consumes the '\n' terminator but leaves the '\r' in the buffer.
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
                value.pop_back();
            while (!key.empty() && (key.back() == '\r' || key.back() == '\n'))
                key.pop_back();

            data[key] = value;
            Serial.printf("Loaded key: %s, value: %s\n", key.c_str(), value.c_str());
        }
        file.close();
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
        for (const auto& kv : data)
            emitted[kv.first] = false;

        File wf = LittleFS.open(filename.c_str(), "w");
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
            auto it = data.find(key);
            if (it != data.end()) {
                wf.printf("%s=%s\n", key.c_str(), it->second.c_str());
                emitted[key] = true;
            }
            // Key removed from DataStore — omit the line.
        }

        // Append any keys that were not present in the original file.
        for (const auto& kv : data)
            if (!emitted[kv.first])
                wf.printf("%s=%s\n", kv.first.c_str(), kv.second.c_str());

        wf.close();
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