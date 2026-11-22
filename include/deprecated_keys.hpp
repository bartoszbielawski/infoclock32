#pragma once

// Config keys and placeholders that still work but are scheduled for removal.
// Pure C++14, host-tested in tests/host/deprecated_keys_check.cpp.
//
// Computed on demand from the live config (nothing cached), so saving in /edit
// updates the web banner at once. Empty values are ignored: devices ship with
// blank `ow_api_key=` lines that must not warn forever.

#include <string>
#include <vector>

#include <config_view.hpp>
#include <weather_locations.hpp>

// Version in which the deprecated names stop working.
#define DEPRECATED_REMOVAL_VERSION "0.9.0"

struct DeprecatedUse {
    std::string what;    // the key or placeholder as the user wrote it
    std::string advice;  // what to do instead, ready to print
};

inline std::vector<DeprecatedUse> find_deprecated(const ConfigView& cfg)
{
    std::vector<DeprecatedUse> out;
    const std::string removal = " (removed in " DEPRECATED_REMOVAL_VERSION ")";
    auto nonEmpty = [&](const char* k) { return !weather_detail::trim(cfg.value(k)).empty(); };

    struct Rename { const char* old; const char* now; };
    static const Rename kRenames[] = {
        { "ow_api_key",      "weather_api_key" },
        { "ow_interval_min", "weather_interval_min" },
        { "ow_city_id",      "weather_<id>_city_id" },
    };
    for (const Rename& r : kRenames)
        if (nonEmpty(r.old))
            out.push_back({ r.old, std::string("use ") + r.now + removal });

    // Single-location keys are plain config while they are the only location;
    // once weather_<id>_* exists they are ignored, which is worth saying.
    const WeatherLocationSet locs = parse_weather_locations(cfg);
    if (locs.named) {
        static const char* const kLegacy[] = { "weather_lat", "weather_lon", "weather_name", "weather_city_id" };
        for (const char* k : kLegacy)
            if (nonEmpty(k))
                out.push_back({ k, "ignored while weather_<id>_* locations exist; remove it" });

        // The unprefixed placeholders mirror the first location for now.
        static const char* const kPlaceholders[] = { "{weather_temp}", "{weather_desc}", "{weather_id}" };
        for (const std::string& key : cfg.keys("message_")) {
            if (!weather_detail::ends_with(key, "_text")) continue;
            const std::string text = cfg.value(key);
            for (const char* ph : kPlaceholders)
                if (text.find(ph) != std::string::npos)
                    out.push_back({ std::string(ph) + " in " + key,
                                    "use {weather_<id>_" + std::string(ph + 9) + removal });
        }
    }
    return out;
}
