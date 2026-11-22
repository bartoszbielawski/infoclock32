// Host-side tests for include/deprecated_keys.hpp.
#include <deprecated_keys.hpp>
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

static bool has(const std::vector<DeprecatedUse>& u, const std::string& what)
{
    for (const auto& d : u) if (d.what == what) return true;
    return false;
}

int main()
{
    CHECK(find_deprecated(view(Map())).empty(), "empty config: nothing to warn about");

    // Blank values shipped in older data/config.txt must not warn.
    CHECK(find_deprecated(view(Map{{"ow_api_key", ""}, {"ow_city_id", ""}, {"ow_interval_min", " "}})).empty(),
          "empty old keys ignored");

    {
        auto u = find_deprecated(view(Map{{"ow_api_key", "k"}, {"ow_city_id", "1"}, {"ow_interval_min", "20"}}));
        CHECK(u.size() == 3, "three renamed keys, got %zu", u.size());
        CHECK(has(u, "ow_api_key") && has(u, "ow_city_id") && has(u, "ow_interval_min"), "names reported");
        CHECK(u[0].advice.find("weather_api_key") != std::string::npos, "advice names the replacement");
        CHECK(u[0].advice.find(DEPRECATED_REMOVAL_VERSION) != std::string::npos, "advice names the removal version");
    }

    // Legacy single-location keys are fine on their own...
    CHECK(find_deprecated(view(Map{{"weather_lat", "1"}, {"weather_lon", "2"}, {"weather_name", "x"},
                                   {"message_a_text", "{weather_temp}"}})).empty(),
          "legacy keys and placeholders are not flagged while single-location");

    // ...but flagged once named locations exist.
    {
        Map m = {{"weather_geneva_lat", "46"}, {"weather_geneva_lon", "6"},
                 {"weather_lat", "1"}, {"weather_lon", "2"}, {"weather_name", "x"},
                 {"message_a_text", "It is {weather_temp} outside"},
                 {"message_b_text", "{weather_desc} and {weather_id}"},
                 {"message_c_text", "{weather_geneva_temp} is fine"},
                 {"message_d_other", "{weather_temp}"}};
        auto u = find_deprecated(view(m));
        CHECK(has(u, "weather_lat") && has(u, "weather_lon") && has(u, "weather_name"), "legacy keys ignored warning");
        CHECK(has(u, "{weather_temp} in message_a_text"), "placeholder in message a");
        CHECK(has(u, "{weather_desc} in message_b_text") && has(u, "{weather_id} in message_b_text"), "both in b");
        CHECK(!has(u, "{weather_geneva_temp} in message_c_text"), "per-location placeholder is fine");
        CHECK(u.size() == 6, "six warnings, got %zu", u.size());
        for (const auto& d : u)
            if (d.what.find("message_a_text") != std::string::npos)
                CHECK(d.advice.find("{weather_<id>_temp}") != std::string::npos, "placeholder advice: %s", d.advice.c_str());
    }

    if (failures) { printf("%d deprecated key check(s) FAILED\n", failures); return 1; }
    printf("all deprecated key checks passed\n");
    return 0;
}
