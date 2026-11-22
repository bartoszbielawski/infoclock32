#pragma once

// Weather locations from the config. Pure C++14 (no Arduino deps), host-tested
// in tests/host/weather_locations_check.cpp.
//
// Named locations are discovered by scanning for keys:
//   weather_<id>_lat / _lon     Open-Meteo coordinates
//   weather_<id>_city_id        OpenWeatherMap city ID
//   weather_<id>_name           label shown on the display (optional)
//   weather_<id>_provider       owm | open-meteo (optional override)
// <id> is [a-z0-9]+, so the global keys (weather_lat, weather_provider,
// weather_api_key, weather_interval_min ...) can never be taken for one.
// Rotation order is the alphabetical order of the ids; at most
// kMaxWeatherLocations are used.
//
// With no named location, the legacy single-location keys (weather_lat/lon,
// sun_lat/lon, weather_city_id, ow_city_id, weather_name) form one implicit
// location with an empty id.

#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include <config_view.hpp>
#include <parse_utils.hpp>

static const size_t kMaxWeatherLocations = 4;

struct WeatherLocation {
    std::string id;        // "" for the legacy single location
    std::string label;     // weather_<id>_name, may be empty
    float lat = NAN;
    float lon = NAN;
    std::string cityId;
    std::string provider;  // "", "owm" or "open-meteo"

    bool hasCoords() const
    {
        return !std::isnan(lat) && !std::isnan(lon) &&
               lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
    }
};

struct WeatherLocationSet {
    std::vector<WeatherLocation> locations;
    bool named = false;       // true when found via weather_<id>_* keys
    size_t ignored = 0;       // valid named locations beyond the cap
};

enum class WeatherBackend { None, Owm, OpenMeteo };

namespace weather_detail {

inline std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
    return s.substr(b, e - b);
}

inline bool valid_id(const std::string& id)
{
    if (id.empty()) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

inline bool ends_with(const std::string& s, const char* suffix)
{
    const std::string x(suffix);
    return s.size() > x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

inline float number(const ConfigView& cfg, const std::string& key)
{
    const std::string v = trim(cfg.value(key));
    return v.empty() ? (float)NAN : parse_value(v, (float)NAN);
}

}  // namespace weather_detail

inline std::string weather_key(const std::string& id, const char* field)
{
    return "weather_" + id + "_" + field;
}

// New key first, then the old `ow_*` name; empty values count as unset.
inline std::string config_with_alias(const ConfigView& cfg, const char* key, const char* oldKey)
{
    std::string v = weather_detail::trim(cfg.value(key));
    if (v.empty()) v = weather_detail::trim(cfg.value(oldKey));
    return v;
}

inline std::string weather_api_key(const ConfigView& cfg)
{
    return config_with_alias(cfg, "weather_api_key", "ow_api_key");
}

// Minutes between fetches of every location, clamped to 10..1440.
inline int weather_interval_min(const ConfigView& cfg)
{
    const std::string v = config_with_alias(cfg, "weather_interval_min", "ow_interval_min");
    int m = v.empty() ? 30 : parse_value(v, 30);
    return m < 10 ? 10 : (m > 1440 ? 1440 : m);
}

inline WeatherLocationSet parse_weather_locations(const ConfigView& cfg)
{
    using namespace weather_detail;
    WeatherLocationSet out;

    static const char* const kSuffixes[] = { "_lat", "_lon", "_city_id" };
    const std::string prefix = "weather_";
    std::set<std::string> ids;
    for (const std::string& k : cfg.keys(prefix)) {
        const std::string rest = k.substr(prefix.size());
        for (const char* suffix : kSuffixes) {
            if (!ends_with(rest, suffix)) continue;
            const std::string id = rest.substr(0, rest.size() - std::string(suffix).size());
            if (valid_id(id)) ids.insert(id);
        }
    }

    for (const std::string& id : ids) {
        WeatherLocation l;
        l.id = id;
        l.lat = number(cfg, weather_key(id, "lat"));
        l.lon = number(cfg, weather_key(id, "lon"));
        l.cityId = trim(cfg.value(weather_key(id, "city_id")));
        l.label = trim(cfg.value(weather_key(id, "name")));
        l.provider = trim(cfg.value(weather_key(id, "provider")));
        if (!l.hasCoords() && l.cityId.empty()) continue;
        if (out.locations.size() >= kMaxWeatherLocations) { out.ignored++; continue; }
        out.locations.push_back(l);
    }
    if (!out.locations.empty()) { out.named = true; return out; }

    WeatherLocation l;
    l.lat = number(cfg, "weather_lat");
    l.lon = number(cfg, "weather_lon");
    if (std::isnan(l.lat) || std::isnan(l.lon)) {
        l.lat = number(cfg, "sun_lat");
        l.lon = number(cfg, "sun_lon");
    }
    l.cityId = config_with_alias(cfg, "weather_city_id", "ow_city_id");
    l.label = trim(cfg.value("weather_name"));
    if (l.hasCoords() || !l.cityId.empty()) out.locations.push_back(l);
    return out;
}

// globalProvider: the weather_provider key (auto, owm, open-meteo).
inline WeatherBackend choose_weather_backend(const WeatherLocation& l,
                                             const std::string& globalProvider,
                                             bool hasApiKey)
{
    const std::string p = l.provider.empty() ? globalProvider : l.provider;
    const bool owmReady = hasApiKey && !l.cityId.empty();
    const bool omReady  = l.hasCoords();
    if (p == "owm")        return owmReady ? WeatherBackend::Owm : WeatherBackend::None;
    if (p == "open-meteo") return omReady  ? WeatherBackend::OpenMeteo : WeatherBackend::None;
    if (owmReady) return WeatherBackend::Owm;
    if (omReady)  return WeatherBackend::OpenMeteo;
    return WeatherBackend::None;
}

// Round-robin: the next index after `cursor` (wrapping) whose entry is ready,
// or -1 when none is. Updates `cursor` to the returned index.
inline int next_ready_location(const std::vector<bool>& ready, size_t& cursor)
{
    const size_t n = ready.size();
    for (size_t step = 1; step <= n; step++) {
        const size_t i = (cursor + step) % n;
        if (ready[i]) { cursor = i; return (int)i; }
    }
    return -1;
}
