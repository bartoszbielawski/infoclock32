
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

// OpenWeatherMap API endpoints stored in flash (PROGMEM)
static const char OW_WEATHER_API_CURRENT[]  PROGMEM = "https://api.openweathermap.org/data/2.5/weather?id=%s&appid=%s&units=metric&lang=%s";
static const char OW_WEATHER_API_FORECAST[] PROGMEM = "https://api.openweathermap.org/data/2.5/forecast?id=%s&appid=%s&units=metric&lang=%s";

static std::map<std::string, std::string> parseJsonWithPredicate(const String &json, const std::set<std::string> &keys)
{
    // predicate used by MapCollector to decide which keys to keep, ignore values
    auto keep_pred = [&keys](const std::string& path, const std::string& value) -> bool {
        return keys.find(std::string(path)) != keys.end();
    };

    MapCollector mc(keep_pred);   
    for (auto ch : json) {
        // missing error handling here
        mc.parse(ch);
    }

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

    String output;
    auto response = HttpUtils::httpGet(url, output, true);
    if (response != 200)
    {
        logPrintf("WTH", "current weather HTTP GET failed: %d", response);
        return std::string();
    }

    auto currentWeather = parseJsonWithPredicate(output, weatherKeys);

    // read forecast
    snprintf(url, sizeof(url), OW_WEATHER_API_FORECAST, cityId.c_str(), apiKey.c_str(), lang);
    response = HttpUtils::httpGet(url, output, true);
    if (response != 200)
    {
        logPrintf("WTH", "forecast HTTP GET failed: %d", response);
        return std::string();
    }

    auto forecastWeather = parseJsonWithPredicate(output, forecastKeys);

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

            auto newWeather = readWeatherFromOWM(iconIndex);
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