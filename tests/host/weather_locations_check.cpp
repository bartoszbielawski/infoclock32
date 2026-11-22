// Host-side tests for include/weather_locations.hpp.
#include <weather_locations.hpp>
#include <cstdio>
#include <map>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

using Map = std::map<std::string, std::string>;

static ConfigView view(const Map& m)
{
    ConfigView v;
    v.get = [&m](const std::string& k) {
        auto it = m.find(k);
        return it == m.end() ? std::string() : it->second;
    };
    v.keysWithPrefix = [&m](const std::string& p) {
        std::vector<std::string> keys;
        for (const auto& kv : m)
            if (kv.first.compare(0, p.size(), p) == 0) keys.push_back(kv.first);
        return keys;
    };
    return v;
}

int main()
{
    // Named locations: alphabetical order, labels, coordinates and city ids.
    {
        Map m = {
            {"weather_krakow_lat", "50.06"}, {"weather_krakow_lon", "19.94"},
            {"weather_krakow_city_id", "3094802"}, {"weather_krakow_name", " Krakow "},
            {"weather_geneva_lat", "46.2"}, {"weather_geneva_lon", "6.14"},
            {"weather_provider", "auto"}, {"weather_api_key", "k"}, {"weather_interval_min", "20"},
            {"weather_lat", "1"}, {"weather_lon", "2"},
        };
        auto s = parse_weather_locations(view(m));
        CHECK(s.named && s.locations.size() == 2, "two named locations, got %zu", s.locations.size());
        CHECK(s.locations[0].id == "geneva" && s.locations[1].id == "krakow", "alphabetical order");
        CHECK(s.locations[1].label == "Krakow", "label trimmed: '%s'", s.locations[1].label.c_str());
        CHECK(s.locations[1].cityId == "3094802", "city id");
        CHECK(s.locations[0].hasCoords() && s.locations[0].lat > 46.1f, "coords parsed");
        CHECK(s.locations[0].label.empty(), "no label by default");
    }

    // Global keys never look like locations; incomplete and out-of-range ones are skipped.
    {
        Map m = {
            {"weather_lat", "46"}, {"weather_lon", "6"}, {"weather_city_id", "1"},
            {"weather_provider", "owm"}, {"weather_name", "x"}, {"weather_api_key", "k"},
            {"weather_interval_min", "30"},
            {"weather_half_lat", "46"},                      // no lon, no city id
            {"weather_far_lat", "91"}, {"weather_far_lon", "0"},
            {"weather_Bad_lat", "1"}, {"weather_Bad_lon", "1"},   // uppercase id
            {"weather_a_b_lat", "1"}, {"weather_a_b_lon", "1"},   // underscore id
        };
        auto s = parse_weather_locations(view(m));
        CHECK(!s.named, "no named location");
        CHECK(s.locations.size() == 1 && s.locations[0].id.empty(), "falls back to legacy");
        CHECK(s.locations[0].label == "x" && s.locations[0].cityId == "1", "legacy fields");
    }

    // A named location may have only a city id (OWM) and counts as valid.
    {
        Map m = {{"weather_paris_city_id", "2988507"}};
        auto s = parse_weather_locations(view(m));
        CHECK(s.named && s.locations.size() == 1 && !s.locations[0].hasCoords(), "city-id only location");
    }

    // Cap at kMaxWeatherLocations.
    {
        Map m;
        for (char c = 'a'; c <= 'f'; c++) {
            m[std::string("weather_") + c + "_lat"] = "10";
            m[std::string("weather_") + c + "_lon"] = "10";
        }
        auto s = parse_weather_locations(view(m));
        CHECK(s.locations.size() == kMaxWeatherLocations, "capped at %zu", kMaxWeatherLocations);
        CHECK(s.ignored == 2, "two ignored, got %zu", s.ignored);
        CHECK(s.locations.back().id == "d", "first four kept");
    }

    // Legacy: sun_lat/sun_lon fallback and the old ow_city_id alias.
    {
        Map m = {{"sun_lat", "46.2"}, {"sun_lon", "6.1"}, {"ow_city_id", "99"}};
        auto s = parse_weather_locations(view(m));
        CHECK(s.locations.size() == 1 && s.locations[0].hasCoords(), "sun_lat fallback");
        CHECK(s.locations[0].cityId == "99", "ow_city_id alias");
        Map m2 = {{"weather_city_id", "7"}, {"ow_city_id", "99"}};
        CHECK(parse_weather_locations(view(m2)).locations[0].cityId == "7", "new key wins over alias");
    }

    // Nothing configured.
    CHECK(parse_weather_locations(view(Map())).locations.empty(), "empty config, no location");

    // Global keys with aliases.
    {
        Map m = {{"ow_api_key", "old"}};
        CHECK(weather_api_key(view(m)) == "old", "api key alias");
        m["weather_api_key"] = "new";
        CHECK(weather_api_key(view(m)) == "new", "new api key wins");
        m["weather_api_key"] = "";
        CHECK(weather_api_key(view(m)) == "old", "empty new key falls back");
        CHECK(weather_interval_min(view(Map())) == 30, "default interval");
        CHECK(weather_interval_min(view(Map{{"ow_interval_min", "15"}})) == 15, "interval alias");
        CHECK(weather_interval_min(view(Map{{"weather_interval_min", "1"}})) == 10, "interval min clamp");
        CHECK(weather_interval_min(view(Map{{"weather_interval_min", "99999"}})) == 1440, "interval max clamp");
    }

    // Backend choice.
    {
        WeatherLocation both;  both.lat = 1; both.lon = 1; both.cityId = "5";
        WeatherLocation om;    om.lat = 1;   om.lon = 1;
        WeatherLocation owm;   owm.cityId = "5";
        CHECK(choose_weather_backend(both, "auto", true) == WeatherBackend::Owm, "auto prefers OWM");
        CHECK(choose_weather_backend(both, "auto", false) == WeatherBackend::OpenMeteo, "no key: Open-Meteo");
        CHECK(choose_weather_backend(both, "open-meteo", true) == WeatherBackend::OpenMeteo, "forced Open-Meteo");
        CHECK(choose_weather_backend(om, "owm", true) == WeatherBackend::None, "forced OWM without city");
        CHECK(choose_weather_backend(owm, "auto", false) == WeatherBackend::None, "OWM without key");
        CHECK(choose_weather_backend(owm, "auto", true) == WeatherBackend::Owm, "OWM ready");
        both.provider = "open-meteo";
        CHECK(choose_weather_backend(both, "owm", true) == WeatherBackend::OpenMeteo, "per-location override");
    }

    // Rotation.
    {
        size_t cursor = (size_t)-1;
        std::vector<bool> ready = {true, false, true};
        CHECK(next_ready_location(ready, cursor) == 0, "starts at 0");
        CHECK(next_ready_location(ready, cursor) == 2, "skips not-ready");
        CHECK(next_ready_location(ready, cursor) == 0, "wraps");
        ready = {false, false};
        CHECK(next_ready_location(ready, cursor) == -1, "none ready");
        std::vector<bool> none;
        CHECK(next_ready_location(none, cursor) == -1, "empty list");
    }

    if (failures) { printf("%d weather location check(s) FAILED\n", failures); return 1; }
    printf("all weather location checks passed\n");
    return 0;
}
