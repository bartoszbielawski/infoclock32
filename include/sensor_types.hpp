#pragma once

// Pure parsing for the `sensors` config key — no DataStore, no Arduino, no
// driver includes — so it can be unit tested on the host (see
// tests/host/sensor_factory_check.cpp) without pulling in the sensor drivers.
//
// parseSensorTypes() splits a comma-separated type list ("bme280,scd4x",
// "bme280 , SHT31") into a de-duplicated vector of lowercased types. Empty
// entries and duplicates are dropped (first position wins). The cap on how
// many sensors may actually be instantiated (kMaxSensors) is applied by the
// caller (createSensorList in sensor_factory.hpp), not here.

#include <string>
#include <vector>
#include <cctype>

// How many sensors may be active at once. The I2C bus and the round-robin
// display cycle are the practical limits; beyond this the readings would be
// stale by the time a sensor refreshes again.
inline constexpr int kMaxSensors = 4;

inline std::vector<std::string> parseSensorTypes(const std::string& list)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < list.size())
    {
        size_t comma = list.find(',', pos);
        if (comma == std::string::npos) comma = list.size();

        size_t b = pos, e = comma;
        while (b < e && isspace((unsigned char)list[b])) b++;
        while (e > b && isspace((unsigned char)list[e - 1])) e--;
        std::string tok = list.substr(b, e - b);
        for (size_t i = 0; i < tok.size(); i++)
            tok[i] = (char)tolower((unsigned char)tok[i]);

        if (!tok.empty())
        {
            bool dup = false;
            for (size_t i = 0; i < out.size(); i++) dup = dup || out[i] == tok;
            if (!dup) out.push_back(tok);
        }
        pos = comma + 1;
    }
    return out;
}
// Replace the `auto` token of a parsed `sensors` list by `detected` (the types
// found on the I2C bus, in report order), in place, then drop duplicates again
// so `auto,ds18b20` or `bme280,auto` behave as expected. Without `auto` the
// list is returned unchanged.
inline std::vector<std::string> expandAutoTypes(const std::vector<std::string>& types,
                                                const std::vector<std::string>& detected)
{
    std::vector<std::string> out;
    auto push = [&](const std::string& t) {
        for (size_t i = 0; i < out.size(); i++) if (out[i] == t) return;
        out.push_back(t);
    };
    for (const auto& t : types)
    {
        if (t == "auto") for (const auto& d : detected) push(d);
        else push(t);
    }
    return out;
}

inline bool listHasAuto(const std::vector<std::string>& types)
{
    for (const auto& t : types) if (t == "auto") return true;
    return false;
}
