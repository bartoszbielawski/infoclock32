
#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <set>

#include <AJSP.hpp>
#include <MapCollector.hpp>

#include <data_store.hpp>
#include <parse_utils.hpp>
#include <http_utils.hpp>
#include <graphic_utils.hpp>
#include <logger.hpp>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>

// OpenWeatherMap API endpoints stored in flash (PROGMEM)
static const char OW_WEATHER_API_CURRENT[]  PROGMEM = "https://api.openweathermap.org/data/2.5/weather?id=%s&appid=%s&units=metric";
static const char OW_WEATHER_API_FORECAST[] PROGMEM = "https://api.openweathermap.org/data/2.5/forecast?id=%s&appid=%s&units=metric";

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
    "/root/main/temp"
};

// OWM forecast returns 3-hour slots; index 2 = ~6 hours ahead from the current slot.
static const std::set<std::string> forecastKeys = {
    "/root/list/2/main/temp",
    "/root/list/2/weather/0/description",
    "/root/city/name"
};

static std::string readWeatherFromOWM()
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
    char url[128];
    snprintf(url, sizeof(url), OW_WEATHER_API_CURRENT, cityId.c_str(), apiKey.c_str());

    String output;
    auto response = HttpUtils::httpGet(url, output, true);
    if (response != 200)
    {
        logPrintf("WTH", "current weather HTTP GET failed: %d", response);
        return std::string();
    }

    auto currentWeather = parseJsonWithPredicate(output, weatherKeys);

    // read forecast
    snprintf(url, sizeof(url), OW_WEATHER_API_FORECAST, cityId.c_str(), apiKey.c_str());
    response = HttpUtils::httpGet(url, output, true);
    if (response != 200)
    {
        logPrintf("WTH", "forecast HTTP GET failed: %d", response);
        return std::string();
    }

    auto forecastWeather = parseJsonWithPredicate(output, forecastKeys);

    // create result string with the following format:
    // Name: temp ^C (forecast temperature ^C, forecast description))

    // format string stored in flash (PROGMEM) to save RAM
    static const char WEATHER_FMT[] PROGMEM = "%s: %.1fC (%.1fC, %s)";

    float currentTemp  = parse_value(currentWeather["/root/main/temp"],         NAN);
    float forecastTemp = parse_value(forecastWeather["/root/list/2/main/temp"], NAN);
    if (std::isnan(currentTemp) || std::isnan(forecastTemp))
    {
        logPrintf("WTH", "failed to parse temperature values from API response");
        return std::string();
    }

    const std::string& cityName = forecastWeather["/root/city/name"];
    const std::string& description = forecastWeather["/root/list/2/weather/0/description"];
    if (cityName.empty() || description.empty())
    {
        logPrintf("WTH", "missing city name or description in forecast response");
        return std::string();
    }

    char weatherInfo[256];
    snprintf_P(weatherInfo, sizeof(weatherInfo), WEATHER_FMT,
        cityName.c_str(),
        currentTemp,
        forecastTemp,
        description.c_str()
    );

    return weatherInfo;
}

void open_weather_map_task(void *parameter)
{
    std::string messageToBeDisplayed;
    time_t last_weather_update = 0;

    auto& rmd = ResourceManager<LMDS>::getInstance();
    auto& matrix = rmd.getResourceRef();

    while (true)
    {
        if (difftime(time(nullptr), last_weather_update) > 30*60) // update weather every 30 minutes
        {
            auto newWeather = readWeatherFromOWM();
            if (not newWeather.empty())
            {
                messageToBeDisplayed = newWeather;
                last_weather_update = time(nullptr);
            }               
        }

        if (messageToBeDisplayed.empty())
        {
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        logPrintf("WTH", "%s", messageToBeDisplayed.c_str());

        if (not rmd.make_access_request())
        {
            logPrintf("WTH", "Failed to get access to display");
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }


        scrollMessage(messageToBeDisplayed, matrix, 50);
        rmd.release_access();

        vTaskDelay(20000 / portTICK_PERIOD_MS); // wait 20 s before requesting display again
    }
}