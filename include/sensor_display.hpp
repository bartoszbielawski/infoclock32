#pragma once
#ifndef SENSOR_DISPLAY_HPP
#define SENSOR_DISPLAY_HPP

#include <string>
#include <vector>
#include <stdint.h>
#include <cstdio>
#include <cctype>
#include <pressure_trend.hpp>
#include <weather_icons.hpp>

// Display formatting for the sensor task: parse the sensor_display
// config list and turn measurements into LED-matrix text. Pure C++ — no
// Arduino/FreeRTOS — so it can be unit tested on the host
// (see tests/host/sensor_display_check.cpp).

enum SensorDisplayItem
{
    SDI_TEMP = 0,
    SDI_HUMIDITY,
    SDI_PRESSURE,
    SDI_CO2,
    SDI_COUNT
};

// Default value of the sensor_display key: what scrolled before the key existed.
static const char kSensorDisplayDefault[] = "temp,pressure";

// Temperature is always shown with one fractional digit — the temp_decimals
// config key was removed in the multi-sensor rework.
//
// LED-font degree glyph (Adafruit 5x7 font, code 0xF7). The string literals
// keep the hex escape from swallowing the following 'C'.
inline const char* ledTempFormat()
{
    return "%.1f\xF7" "C";
}

// RuntimeStore / {temp_c} placeholder format (UTF-8 degree sign).
inline const char* runtimeTempFormat()
{
    return "%.1f\xC2\xB0" "C";
}

inline std::string formatTempLine(float tempC)
{
    char buf[16];
    snprintf(buf, sizeof(buf), ledTempFormat(), tempC);
    return buf;
}

inline std::string formatHumidityLine(float rh)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f%%RH", rh);
    return buf;
}

inline std::string formatPressureLine(float hpa)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f hPa", hpa);
    return buf;
}

inline std::string formatCo2Line(int ppm)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%dppmCO2", ppm);
    return buf;
}

// True when the sensor described by its capability flags can provide the item.
inline bool itemAvailable(SensorDisplayItem item, bool hasHumidity, bool hasPressure, bool hasCo2)
{
    switch (item)
    {
        case SDI_HUMIDITY: return hasHumidity;
        case SDI_PRESSURE: return hasPressure;
        case SDI_CO2:      return hasCo2;
        default:           return true;   // temperature is always there
    }
}

// Canonical name of a single item.
inline const char* sensorDisplayName(SensorDisplayItem item)
{
    static const char* names[SDI_COUNT] = { "temp", "humidity", "pressure", "co2" };
    return names[item];
}

// Canonical comma-separated form of an item list (for the web UI round-trip).
inline std::string displayListString(const std::vector<SensorDisplayItem>& items)
{
    std::string out;
    for (size_t i = 0; i < items.size(); i++)
    {
        if (i) out += ',';
        out += sensorDisplayName(items[i]);
    }
    return out;
}

// Parse a comma-separated list ("temp, humidity", "humidity") into ordered
// display items. Tokens are lowercased and trimmed; empty entries, unknown
// tokens and duplicates are dropped (first occurrence wins).
inline std::vector<SensorDisplayItem> parseDisplayList(const std::string& list)
{
    std::vector<SensorDisplayItem> out;
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

        SensorDisplayItem item;
        bool known = true;
        if (tok == "temp" || tok == "temperature")      item = SDI_TEMP;
        else if (tok == "humidity" || tok == "rh")      item = SDI_HUMIDITY;
        else if (tok == "pressure" || tok == "hpa")     item = SDI_PRESSURE;
        else if (tok == "co2")                          item = SDI_CO2;
        else                                            known = false;

        if (known)
        {
            bool dup = false;
            for (size_t i = 0; i < out.size(); i++) dup = dup || out[i] == item;
            if (!dup) out.push_back(item);
        }
        pos = comma + 1;
    }
    return out;
}

// Barometric trend arrow glyph for the pressure readout.
inline const uint8_t* trendIconFor(TrendKind kind)
{
    switch (kind)
    {
        case TREND_RISING_FAST:  return kWeatherIcons[WI_ARROW_UP_FAST];
        case TREND_RISING:       return kWeatherIcons[WI_ARROW_UP];
        case TREND_FALLING:      return kWeatherIcons[WI_ARROW_DOWN];
        case TREND_FALLING_FAST: return kWeatherIcons[WI_ARROW_DOWN_FAST];
        default:                 return kWeatherIcons[WI_TREND_STEADY];
    }
}

#endif // SENSOR_DISPLAY_HPP
