
#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <set>

#include <AJSP.hpp>
#include <MapCollector.hpp>

#include <data_store.hpp>
#include <http_utils.hpp>
#include <graphic_utils.hpp>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>

// OpenWeatherMap API endpoints stored in flash (PROGMEM)
static const char OW_WEATHER_API_CURRENT[]  PROGMEM = "http://api.openweathermap.org/data/2.5/weather?id=%s&appid=%s&units=metric";
static const char OW_WEATHER_API_FORECAST[] PROGMEM = "http://api.openweathermap.org/data/2.5/forecast?id=%s&appid=%s&units=metric";

std::map<std::string, std::string> parseJsonWithPredicate(const String &json, const std::set<std::string> &keys)
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

static const std::set<std::string> forecastKeys = {
    "/root/list/2/main/temp",
    "/root/list/2/weather/0/description",
    "/root/city/name"
};

std::string readWeatherFromOWM()
{
    auto apiKey = DataStore::getInstance().get_value("ow_api_key", "");
    auto cityId = DataStore::getInstance().get_value("ow_city_id", "");

    //read current weather
    char url[128];
    snprintf(url, sizeof(url), OW_WEATHER_API_CURRENT, cityId.c_str(), apiKey.c_str());

    String output;
    auto response = HttpUtils::httpGet(url, output, false);
    if (response != 200)
    {
        Serial.printf("HTTP GET failed, response: %d\n", response);
        vTaskDelay(60000 / portTICK_PERIOD_MS); // wait a minute before retrying
        return std::string();
    }

    auto currentWeather = parseJsonWithPredicate(output, weatherKeys);
    
    //read forcast
    snprintf(url, sizeof(url), OW_WEATHER_API_FORECAST, cityId.c_str(), apiKey.c_str());
    response = HttpUtils::httpGet(url, output, false);
    if (response != 200)
    {
        Serial.printf("HTTP GET failed, response: %d\n", response);
        vTaskDelay(60000 / portTICK_PERIOD_MS); // wait a minute before retrying
        return std::string();
    }

    auto foracastWeather = parseJsonWithPredicate(output, forecastKeys);

    //create result string witht he following format:
    // Name: temp ^C (forecast temperature ^C, forecast description))

    // format string stored in flash (PROGMEM) to save RAM
    static const char WEATHER_FMT[] PROGMEM = "%s: %.1fC (%.1fC, %s)";

    char weatherInfo[256];
    snprintf_P(weatherInfo, sizeof(weatherInfo), WEATHER_FMT,
        foracastWeather["/root/city/name"].c_str(),
        std::stof(currentWeather["/root/main/temp"]),
        std::stof(foracastWeather["/root/list/2/main/temp"]),
        foracastWeather["/root/list/2/weather/0/description"].c_str()
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
        if (difftime(time(nullptr), last_weather_update) > 900)
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

        Serial.printf("Weather: %s\n", messageToBeDisplayed.c_str());

        if (not rmd.make_access_request())
        {
            Serial.println("WeatherDisplay: Failed to get access to display");
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }


        scrollMessage(messageToBeDisplayed, matrix, 50);
        rmd.release_access();

        vTaskDelay(20000 / portTICK_PERIOD_MS); // update every minute
    }
}