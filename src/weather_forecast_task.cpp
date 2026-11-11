
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
// KB of heap instead of a ~55 KB TLS session. Location: weather_lat/lon, or
// the sun_lat/lon the sunrise task already uses.

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

static bool weather_location(float& lat, float& lon)
{
    auto& ds = DataStore::getInstance();
    lat = ds.get_value<float>("weather_lat", NAN);
    lon = ds.get_value<float>("weather_lon", NAN);
    if (std::isnan(lat) || std::isnan(lon)) {
        lat = ds.get_value<float>("sun_lat", NAN);
        lon = ds.get_value<float>("sun_lon", NAN);
    }
    return !std::isnan(lat) && !std::isnan(lon) &&
           lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

enum class WeatherProvider { None, Owm, OpenMeteo };

// weather_provider: auto (default: OWM when its key and city are set,
// otherwise Open-Meteo when a location is set), owm, open-meteo.
static WeatherProvider weather_provider()
{
    auto& ds = DataStore::getInstance();
    const std::string p = ds.get_value("weather_provider", "auto");
    const bool owmReady = !ds.get_value("ow_api_key", "").empty() &&
                          !ds.get_value("ow_city_id", "").empty();
    float lat, lon;
    const bool omReady = weather_location(lat, lon);
    if (p == "owm")        return owmReady ? WeatherProvider::Owm : WeatherProvider::None;
    if (p == "open-meteo") return omReady ? WeatherProvider::OpenMeteo : WeatherProvider::None;
    if (owmReady) return WeatherProvider::Owm;
    if (omReady)  return WeatherProvider::OpenMeteo;
    return WeatherProvider::None;
}

bool weather_configured() { return weather_provider() != WeatherProvider::None; }

static std::string readWeatherFromOpenMeteo(int& iconIndex)
{
    auto& ds = DataStore::getInstance();
    float lat, lon;
    if (!weather_location(lat, lon)) {
        logPrintf("WTH", "no location: set weather_lat/weather_lon (or sun_lat/sun_lon)");
        return std::string();
    }
    char url[256];
    snprintf_P(url, sizeof(url), OPEN_METEO_URL, lat, lon);

    std::map<std::string, std::string> v;
    int response = HttpUtils::httpGetStream(url, [&](Stream& body) {
        v = parseJsonStreamWithPredicate(body, openMeteoKeys);
    });
    if (response != 200) {
        logPrintf("WTH", "Open-Meteo HTTP GET failed: %d", response);
        return std::string();
    }

    const float nowT  = parse_value(v["/root/current/temperature_2m"], NAN);
    const float laterT = parse_value(v["/root/hourly/temperature_2m/6"], NAN);
    const int nowCode  = parse_value(v["/root/current/weather_code"], -1);
    const int laterCode = parse_value(v["/root/hourly/weather_code/6"], -1);
    if (std::isnan(nowT) || std::isnan(laterT) || nowCode < 0 || laterCode < 0) {
        logPrintf("WTH", "Open-Meteo response missing fields");
        return std::string();
    }
    const bool night = parse_value(v["/root/current/is_day"], 1) == 0;
    const Lang lang = parse_lang(ds.get_value("language", "en"));
    const int owmId = wmo_to_owm(nowCode);
    const std::string description = wmo_description(laterCode, lang);
    iconIndex = weatherIconIndex(owmId, night);

    RuntimeStore::getInstance().set("weather_id", std::to_string(owmId));
    RuntimeStore::getInstance().set("weather_desc", description);
    char t[16];
    snprintf(t, sizeof(t), "%.1f", nowT);
    RuntimeStore::getInstance().set("weather_temp", t);

    // Same layout as the OWM message; the label is optional (Open-Meteo has
    // no place names).
    const std::string name = ascii_fold(ds.get_value("weather_name", "").c_str());
    char msg[160];
    snprintf(msg, sizeof(msg), "%s%s%.1f\xF7" "C (%.1f\xF7" "C, %s)",
             name.c_str(), name.empty() ? "" : ": ", nowT, laterT, description.c_str());
    logPrintf("WTH", "Open-Meteo: %.1f C now (code %d), %.1f C in 6 h, %s",
              nowT, nowCode, laterT, description.c_str());
    return msg;
}

// Returns the display message, or an empty string on failure.
// iconIndex (out): index into kWeatherIcons for the current condition,
// or -1 if the condition could not be parsed.
static std::string readWeatherFromOWM(int& iconIndex)
{
    auto& ds    = DataStore::getInstance();
    auto apiKey = ds.get_value("ow_api_key", "");
    auto cityId = ds.get_value("ow_city_id", "");

    if (apiKey.empty() || cityId.empty())
    {
        logPrintf("WTH", "ow_api_key or ow_city_id not configured");
        return std::string();
    }

    // read current weather
    const char* lang = owm_lang(parse_lang(ds.get_value("language", "en")));
    char url[160];
    snprintf(url, sizeof(url), OW_WEATHER_API_CURRENT, cityId.c_str(), apiKey.c_str(), lang);

    std::map<std::string, std::string> currentWeather;
    auto response = HttpUtils::httpGetStream(url, [&](Stream& body) {
        currentWeather = parseJsonStreamWithPredicate(body, weatherKeys);
    });
    if (response != 200)
    {
        logPrintf("WTH", "current weather HTTP GET failed: %d", response);
        return std::string();
    }

    // read forecast
    snprintf(url, sizeof(url), OW_WEATHER_API_FORECAST, cityId.c_str(), apiKey.c_str(), lang);
    std::map<std::string, std::string> forecastWeather;
    response = HttpUtils::httpGetStream(url, [&](Stream& body) {
        forecastWeather = parseJsonStreamWithPredicate(body, forecastKeys);
    });
    if (response != 200)
    {
        logPrintf("WTH", "forecast HTTP GET failed: %d", response);
        return std::string();
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
        logPrintf("WTH", "failed to parse temperature values from API response");
        return std::string();
    }

    // The matrix font is ASCII only; localized names/descriptions carry diacritics.
    const std::string cityName = ascii_fold(forecastWeather["/root/city/name"].c_str());
    const std::string description = ascii_fold(forecastWeather["/root/list/2/weather/0/description"].c_str());
    if (cityName.empty() || description.empty())
    {
        logPrintf("WTH", "missing city name or description in forecast response");
        return std::string();
    }

    // Current condition drives the display icon; OWM's icon field carries
    // a d/n suffix distinguishing day from night.
    int conditionId = parse_value(currentWeather["/root/weather/0/id"], -1);
    std::string iconField = currentWeather["/root/weather/0/icon"];
    bool night = !iconField.empty() && iconField.back() == 'n';
    iconIndex = (conditionId > 0) ? weatherIconIndex(conditionId, night) : -1;

    // Publish for {placeholders} in custom messages and MQTT /request
    RuntimeStore::getInstance().set("weather_id", std::to_string(conditionId));
    RuntimeStore::getInstance().set("weather_desc", description);

    char logInfo[256];
    snprintf_P(logInfo, sizeof(logInfo), WEATHER_FMT_LOG,
        cityName.c_str(), currentTemp, forecastTemp, description.c_str());
    logPrintf("WTH", "%s", logInfo);

    char weatherInfo[256];
    snprintf_P(weatherInfo, sizeof(weatherInfo), WEATHER_FMT_DISP,
        cityName.c_str(), currentTemp, forecastTemp, description.c_str());

    return weatherInfo;
}

void open_weather_map_task(void *parameter)
{
    registerTask("Weather", 8192, 120000);
    std::string messageToBeDisplayed;
    int iconIndex = -1;  // -1 = no icon; otherwise index into kWeatherIcons
    // Fetch scheduling on millis(): the wall clock reads ~uptime until NTP
    // syncs. nextFetchMs == 0 means "fetch now" (first pass after boot).
    uint32_t nextFetchMs = 0;
    uint32_t failures = 0;

    auto& rmd = ResourceManager<LMDS>::getInstance();

    while (true)
    {
        task_heartbeat();
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(30000 / portTICK_PERIOD_MS);
            continue;
        }

        if (nextFetchMs == 0 || (int32_t)(millis() - nextFetchMs) >= 0)
        {
            // ow_interval_min: minutes between API calls (default 30, min 10)
            uint32_t intervalMs = (uint32_t)max(10, min(1440,
                DataStore::getInstance().get_value<int>("ow_interval_min", 30))) * (uint32_t)60000;

            const WeatherProvider provider = weather_provider();
            auto newWeather = provider == WeatherProvider::OpenMeteo ? readWeatherFromOpenMeteo(iconIndex)
                            : provider == WeatherProvider::Owm       ? readWeatherFromOWM(iconIndex)
                            : std::string();
            if (not newWeather.empty())
            {
                messageToBeDisplayed = newWeather;
                failures = 0;
                nextFetchMs = millis() + intervalMs;
            }
            else
            {
                // back off 1, 2, 4... min (capped at 10 min / the interval) instead of
                // hitting the API every minute while it is failing
                uint32_t backoffMs = (uint32_t)60000 << min<uint32_t>(failures, 10);
                if (failures < 10) failures++;
                nextFetchMs = millis() + min(min(backoffMs, intervalMs), (uint32_t)600000);
            }
            if (nextFetchMs == 0) nextFetchMs = 1;
        }

        if (messageToBeDisplayed.empty())
        {
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        // Blocking acquire: cover the worst-case wait + scroll hold, then
        // re-anchor the watchdog window once granted (clock-task pattern).
        task_heartbeat_grace(90000);
        if (auto display = rmd.acquire())
        {
            task_heartbeat();
            task_heartbeat_grace(30000);
            const uint8_t* icon = (iconIndex >= 0) ? kWeatherIcons[iconIndex] : nullptr;
            scrollMessage(icon, kWeatherIconWidth, messageToBeDisplayed, display, 20);
        }

        vTaskDelay(60000 / portTICK_PERIOD_MS);

    }
}