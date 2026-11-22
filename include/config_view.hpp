#pragma once

// Read-only window onto the config, so pure headers (weather_locations.hpp,
// deprecated_keys.hpp) can be host-tested without the DataStore. Firmware code
// gets one from dataStoreView() in data_store.hpp.

#include <functional>
#include <string>
#include <vector>

struct ConfigView {
    // All keys starting with the prefix (any order).
    std::function<std::vector<std::string>(const std::string&)> keysWithPrefix;
    // Value of a key, "" when unset.
    std::function<std::string(const std::string&)> get;

    std::string value(const std::string& key) const { return get ? get(key) : std::string(); }
    std::vector<std::string> keys(const std::string& prefix) const
    {
        return keysWithPrefix ? keysWithPrefix(prefix) : std::vector<std::string>();
    }
};
