
#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <set>

#include <AJSP.hpp>
#include <MapCollector.hpp>

#include <data_store.hpp>
#include <parse_utils.hpp>
#include <runtime_store.hpp>
#include <weather_icons.hpp>
#include <WiFi.h>
#include <http_utils.hpp>
#include <graphic_utils.hpp>
#include <logger.hpp>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <task_registry.hpp>
#include <display_strings.hpp>
#include <weather_wmo.hpp>
#include <weather_locations.hpp>
#include <cmath>

// OpenWeatherMap API endpoints stored in flash (PROGMEM)
static const char OW_WEATHER_API_CURRENT[]  PROGMEM = "https://api.openweathermap.org/data/2.5/weather?id=%s&appid=%s&units=metric&lang=%s";
// cnt=3: only slot 2 (~6 h ahead) is used; the default 40 slots are ~16 KB.
static const char OW_WEATHER_API_FORECAST[] PROGMEM = "https://api.openweathermap.org/data/2.5/forecast?id=%s&appid=%s&units=metric&lang=%s&cnt=3";

// Collects the wanted JSON paths straight from the response stream, so the
// body is never buffered whole (one large String needs a contiguous heap
// block, which a fragmented heap may not have - see httpGetStream). Missing
// keys are caught by the caller's checks.
static std::map<std::string, std::string> parseJsonStreamWithPredicate(Stream &body, const std::set<std::string> &keys)
{
    // predicate used by MapCollector to decide which keys to keep, ignore values
    auto keep_pred = [&keys](const std::string& path, const std::string& value) -> bool {
        return keys.find(std::string(path)) != keys.end();
    };

    MapCollector mc(keep_pred);
    char buf[64];
    size_t n;
    // readBytes() waits up to the stream timeout for slow network data and
    // returns 0 at the end of the body (HTTP/1.0: the server closes).
    while ((n = body.readBytes(buf, sizeof(buf))) > 0)
        for (size_t i = 0; i < n; i++)
            mc.parse(buf[i]);

    return mc.getValues();
}

static const std::set<std::string> weatherKeys = {
    "/root/main/temp",
    "/root/weather/0/id",
    "/root/weather/0/icon"
};

// OWM forecast returns 3-hour slots; index 2 = ~6 hours ahead from the current slot.
static const std::set<std::string> forecastKeys = {
    "/root/list/2/main/temp",
    "/root/list/2/weather/0/description",
    "/root/city/name"
};

// ── Open-Meteo (no API key, plain HTTP) ──────────────────────────────────────
// Free and keyless, and served over plain HTTP, so a fetch costs a couple of
// KB of heap instead of a ~55 KB TLS session. Location: weather_<id>_lat/lon
// (see weather_locations.hpp), or the legacy weather_lat/lon / sun_lat/lon.

static const char OPEN_METEO_URL[] PROGMEM =
    "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,weather_code,is_day"
    "&hourly=temperature_2m,weather_code&forecast_hours=7&timezone=auto";

// hourly[0] is the current hour, so [6] is ~6 h ahead (like OWM's slot 2).
static const std::set<std::string> openMeteoKeys = {
    "/root/current/temperature_2m",
    "/root/current/weather_code",
    "/root/current/is_day",
    "/root/hourly/temperature_2m/6",
    "/root/hourly/weather_code/6",
};

struct WeatherResult {
    std::string message;   // display text, empty on failure
    int iconIndex = -1;    // index into kWeatherIcons, -1 = no icon
};

// Log prefix: "[geneva] " for a named location, nothing for the legacy one.
static std::string locTag(const WeatherLocation& loc)
{
    return loc.id.empty() ? std::string() : "[" + loc.id + "] ";
}

static std::string globalProvider()
{
    return DataStore::getInstance().get_value("weather_provider", "auto");
}

bool weather_configured()
{
    const WeatherLocationSet set = parse_weather_locations(dataStoreView());
    const std::string provider = globalProvider();
    const bool hasKey = !weather_api_key(dataStoreView()).empty();
    for (const auto& loc : set.locations)
        if (choose_weather_backend(loc, provider, hasKey) != WeatherBackend::None) return true;
    return false;
}

// Publish for {placeholders} in custom messages and MQTT /request:
// weather_<id>_temp / _desc / _id per named location. `mirror` also fills the
// unprefixed weather_temp / weather_desc / weather_id (legacy single location,
// or the first named one).
// DEPRECATED: the unprefixed mirror is removed in 0.9.0 (see deprecated_keys.hpp).
static void publishWeather(const WeatherLocation& loc, bool mirror, int conditionId,
                           const std::string& description, float nowTemp)
{
    auto& rs = RuntimeStore::getInstance();
    char t[16];
    snprintf(t, sizeof(t), "%.1f", nowTemp);
    const std::string id = std::to_string(conditionId);
    if (!loc.id.empty()) {
        rs.set(weather_key(loc.id, "id"), id);
        rs.set(weather_key(loc.id, "desc"), description);
        rs.set(weather_key(loc.id, "temp"), t);
    }
    if (mirror) {
        rs.set("weather_id", id);
        rs.set("weather_desc", description);
        rs.set("weather_temp", t);
    }
}

static WeatherResult readWeatherFromOpenMeteo(const WeatherLocation& loc, bool mirror)
{
    WeatherResult res;
    auto& ds = DataStore::getInstance();
    char url[256];
    snprintf_P(url, sizeof(url), OPEN_METEO_URL, loc.lat, loc.lon);

    std::map<std::string, std::string> v;
    int response = HttpUtils::httpGetStream(url, [&](Stream& body) {
        v = parseJsonStreamWithPredicate(body, openMeteoKeys);
    });
    if (response != 200) {
        logPrintf("WTH", "%sOpen-Meteo HTTP GET failed: %d", locTag(loc).c_str(), response);
        return res;
    }

    const float nowT  = parse_value(v["/root/current/temperature_2m"], NAN);
    const float laterT = parse_value(v["/root/hourly/temperature_2m/6"], NAN);
    const int nowCode  = parse_value(v["/root/current/weather_code"], -1);
    const int laterCode = parse_value(v["/root/hourly/weather_code/6"], -1);
    if (std::isnan(nowT) || std::isnan(laterT) || nowCode < 0 || laterCode < 0) {
        logPrintf("WTH", "%sOpen-Meteo response missing fields", locTag(loc).c_str());
        return res;
    }
    const bool night = parse_value(v["/root/current/is_day"], 1) == 0;
    const Lang lang = parse_lang(ds.get_value("language", "en"));
    const int owmId = wmo_to_owm(nowCode);
    const std::string description = wmo_description(laterCode, lang);
    res.iconIndex = weatherIconIndex(owmId, night);
    publishWeather(loc, mirror, owmId, description, nowT);

    // Same layout as the OWM message. Open-Meteo has no place names, so the
    // label is weather_<id>_name, else the id (legacy: none).
    const std::string name = ascii_fold((loc.label.empty() ? loc.id : loc.label).c_str());
    char msg[160];
    snprintf(msg, sizeof(msg), "%s%s%.1f\xF7" "C (%.1f\xF7" "C, %s)",
             name.c_str(), name.empty() ? "" : ": ", nowT, laterT, description.c_str());
    logPrintf("WTH", "%sOpen-Meteo: %.1f C now (code %d), %.1f C in 6 h, %s",
              locTag(loc).c_str(), nowT, nowCode, laterT, description.c_str());
    res.message = msg;
    return res;
}

static WeatherResult readWeatherFromOWM(const WeatherLocation& loc, const std::string& apiKey, bool mirror)
{
    WeatherResult res;
    auto& ds = DataStore::getInstance();

    // read current weather
    const char* lang = owm_lang(parse_lang(ds.get_value("language", "en")));
    char url[160];
    snprintf(url, sizeof(url), OW_WEATHER_API_CURRENT, loc.cityId.c_str(), apiKey.c_str(), lang);

    std::map<std::string, std::string> currentWeather;
    auto response = HttpUtils::httpGetStream(url, [&](Stream& body) {
        currentWeather = parseJsonStreamWithPredicate(body, weatherKeys);
    });
    if (response != 200)
    {
        logPrintf("WTH", "%scurrent weather HTTP GET failed: %d", locTag(loc).c_str(), response);
        return res;
    }

    // read forecast
    snprintf(url, sizeof(url), OW_WEATHER_API_FORECAST, loc.cityId.c_str(), apiKey.c_str(), lang);
    std::map<std::string, std::string> forecastWeather;
    response = HttpUtils::httpGetStream(url, [&](Stream& body) {
        forecastWeather = parseJsonStreamWithPredicate(body, forecastKeys);
    });
    if (response != 200)
    {
        logPrintf("WTH", "%sforecast HTTP GET failed: %d", locTag(loc).c_str(), response);
        return res;
    }

    // create result string with the following format:
    // Name: temp ^C (forecast temperature ^C, forecast description))

    // \xF7 is the degree symbol in the LED matrix font.
    // Use UTF-8 \xC2\xB0 (°) in the log string so /log renders correctly in browsers.
    static const char WEATHER_FMT_DISP[] PROGMEM = "%s: %.1f\xF7" "C (%.1f\xF7" "C, %s)";
    static const char WEATHER_FMT_LOG[]  PROGMEM = "%s: %.1f\xC2\xB0" "C (%.1f\xC2\xB0" "C, %s)";

    float currentTemp  = parse_value(currentWeather["/root/main/temp"],         NAN);
    float forecastTemp = parse_value(forecastWeather["/root/list/2/main/temp"], NAN);
    if (std::isnan(currentTemp) || std::isnan(forecastTemp))
    {
        logPrintf("WTH", "%sfailed to parse temperature values from API response", locTag(loc).c_str());
        return res;
    }

    // The matrix font is ASCII only; localized names/descriptions carry diacritics.
    // weather_<id>_name overrides the name the API reports.
    const std::string cityName = ascii_fold(
        (loc.label.empty() ? forecastWeather["/root/city/name"] : loc.label).c_str());
    const std::string description = ascii_fold(forecastWeather["/root/list/2/weather/0/description"].c_str());
    if (cityName.empty() || description.empty())
    {
        logPrintf("WTH", "%smissing city name or description in forecast response", locTag(loc).c_str());
        return res;
    }

    // Current condition drives the display icon; OWM's icon field carries
    // a d/n suffix distinguishing day from night.
    int conditionId = parse_value(currentWeather["/root/weather/0/id"], -1);
    std::string iconField = currentWeather["/root/weather/0/icon"];
    bool night = !iconField.empty() && iconField.back() == 'n';
    res.iconIndex = (conditionId > 0) ? weatherIconIndex(conditionId, night) : -1;

    publishWeather(loc, mirror, conditionId, description, currentTemp);

    char logInfo[256];
    snprintf_P(logInfo, sizeof(logInfo), WEATHER_FMT_LOG,
        cityName.c_str(), currentTemp, forecastTemp, description.c_str());
    logPrintf("WTH", "%s%s", locTag(loc).c_str(), logInfo);

    char weatherInfo[256];
    snprintf_P(weatherInfo, sizeof(weatherInfo), WEATHER_FMT_DISP,
        cityName.c_str(), currentTemp, forecastTemp, description.c_str());

    res.message = weatherInfo;
    return res;
}

// Per-location fetch state. Keyed by location id so a config edit that adds,
// removes or reorders locations keeps the state of the ones that remain.
struct WeatherEntry {
    WeatherResult result;
    uint32_t nextFetchMs = 0;   // 0 = fetch now
    uint32_t failures = 0;
};

void open_weather_map_task(void *parameter)
{
    registerTask("Weather", 7168, 120000);
    std::map<std::string, WeatherEntry> entries;
    size_t cursor = (size_t)-1;     // rotation: last location shown
    size_t lastCount = (size_t)-1;  // to log the location list when it changes

    auto& rmd = ResourceManager<LMDS>::getInstance();

    while (true)
    {
        task_heartbeat();
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(30000 / portTICK_PERIOD_MS);
            continue;
        }

        // Re-read the config every pass: /edit and MQTT /config can add,
        // remove or change locations without a reboot.
        const ConfigView cfg = dataStoreView();
        const WeatherLocationSet set = parse_weather_locations(cfg);
        const std::string provider = globalProvider();
        const std::string apiKey = weather_api_key(cfg);
        const uint32_t intervalMs = (uint32_t)weather_interval_min(cfg) * (uint32_t)60000;

        if (set.locations.size() != lastCount) {
            lastCount = set.locations.size();
            logPrintf("WTH", "%u weather location(s)%s", (unsigned)lastCount, set.named ? "" : " (legacy keys)");
            if (set.ignored)
                logPrintf("WTH", "%u location(s) beyond the limit of %u ignored",
                          (unsigned)set.ignored, (unsigned)kMaxWeatherLocations);
        }

        // Forget locations that left the config.
        for (auto it = entries.begin(); it != entries.end(); ) {
            bool keep = false;
            for (const auto& loc : set.locations) keep = keep || loc.id == it->first;
            it = keep ? std::next(it) : entries.erase(it);
        }

        // Fetch every due location, one request at a time.
        for (size_t i = 0; i < set.locations.size(); i++)
        {
            const WeatherLocation& loc = set.locations[i];
            WeatherEntry& e = entries[loc.id];
            if (!(e.nextFetchMs == 0 || (int32_t)(millis() - e.nextFetchMs) >= 0)) continue;
            task_heartbeat();

            const WeatherBackend backend = choose_weather_backend(loc, provider, !apiKey.empty());
            WeatherResult r;
            if (backend == WeatherBackend::None)
                logPrintf("WTH", "%sno usable backend (needs lat+lon, or city id and weather_api_key)", locTag(loc).c_str());
            else if (backend == WeatherBackend::OpenMeteo)
                r = readWeatherFromOpenMeteo(loc, i == 0);
            else
                r = readWeatherFromOWM(loc, apiKey, i == 0);

            if (backend == WeatherBackend::None)
            {
                e.nextFetchMs = millis() + intervalMs;   // config problem: do not retry every minute
            }
            else if (!r.message.empty())
            {
                e.result = r;
                e.failures = 0;
                e.nextFetchMs = millis() + intervalMs;
            }
            else
            {
                // back off 1, 2, 4... min (capped at 10 min / the interval) instead of
                // hitting the API every minute while it is failing
                uint32_t backoffMs = (uint32_t)60000 << min<uint32_t>(e.failures, 10);
                if (e.failures < 10) e.failures++;
                e.nextFetchMs = millis() + min(min(backoffMs, intervalMs), (uint32_t)600000);
            }
            if (e.nextFetchMs == 0) e.nextFetchMs = 1;
        }

        // Next location that has something to show.
        std::vector<bool> ready;
        for (const auto& loc : set.locations)
            ready.push_back(!entries[loc.id].result.message.empty());
        const int pick = next_ready_location(ready, cursor);
        if (pick < 0)
        {
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }
        const WeatherResult shown = entries[set.locations[pick].id].result;

        // Blocking acquire: cover the worst-case wait + scroll hold, then
        // re-anchor the watchdog window once granted (clock-task pattern).
        task_heartbeat_grace(90000);
        if (auto display = rmd.acquire())
        {
            task_heartbeat();
            task_heartbeat_grace(30000);
            const uint8_t* icon = (shown.iconIndex >= 0) ? kWeatherIcons[shown.iconIndex] : nullptr;
            scrollMessage(icon, kWeatherIconWidth, shown.message, display, 20);
        }

        // One location per pass: with several, shorten the gap so each still
        // comes around reasonably often (never below 20 s).
        const uint32_t gapMs = max<uint32_t>(20000, 60000 / (uint32_t)max<size_t>(set.locations.size(), 1));
        vTaskDelay(gapMs / portTICK_PERIOD_MS);
    }
}
