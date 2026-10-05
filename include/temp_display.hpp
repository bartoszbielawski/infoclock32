#pragma once
#ifndef TEMP_DISPLAY_HPP
#define TEMP_DISPLAY_HPP

#include <string>
#include <vector>
#include <stdint.h>
#include <cstdio>
#include <cctype>
#include <pressure_trend.hpp>
#include <weather_icons.hpp>

// Display formatting for the temperature sensor task: parse the temp_display
// config list and turn measurements into LED-matrix text. Pure C++ — no
// Arduino/FreeRTOS — so it can be unit tested on the host
// (see tests/host/temp_display_check.cpp).

enum TempDisplayItem
{
    TDI_TEMP = 0,
    TDI_HUMIDITY,
    TDI_PRESSURE,
    TDI_COUNT
};

// Default value of the temp_display key: what scrolled before the key existed.
static const char kTempDisplayDefault[] = "temp,pressure";

// LED-font degree glyph (Adafruit 5x7 font, code 0xF7). The string literals
// keep the hex escape from swallowing the following 'C'.
inline const char* ledTempFormat(int decimals)
{
    return decimals == 0 ? "%.0f\xF7" "C" : "%.1f\xF7" "C";
}

// RuntimeStore / {temp_c} placeholder format (UTF-8 degree sign).
inline const char* runtimeTempFormat(int decimals)
{
    return decimals == 0 ? "%.0f\xC2\xB0" "C" : "%.1f\xC2\xB0" "C";
}

inline std::string formatTempLine(float tempC, int decimals)
{
    char buf[16];
    snprintf(buf, sizeof(buf), ledTempFormat(decimals), tempC);
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

// True when the sensor described by its capability flags can provide the item.
inline bool itemAvailable(TempDisplayItem item, bool hasHumidity, bool hasPressure)
{
    switch (item)
    {
        case TDI_HUMIDITY: return hasHumidity;
        case TDI_PRESSURE: return hasPressure;
        default:           return true;   // temperature is always there
    }
}

// Canonical name of a single item.
inline const char* tempDisplayName(TempDisplayItem item)
{
    static const char* names[TDI_COUNT] = { "temp", "humidity", "pressure" };
    return names[item];
}

// Canonical comma-separated form of an item list (for the web UI round-trip).
inline std::string displayListString(const std::vector<TempDisplayItem>& items)
{
    std::string out;
    for (size_t i = 0; i < items.size(); i++)
    {
        if (i) out += ',';
        out += tempDisplayName(items[i]);
    }
    return out;
}

// Parse a comma-separated list ("temp, humidity", "humidity") into ordered
// display items. Tokens are lowercased and trimmed; empty entries, unknown
// tokens and duplicates are dropped (first occurrence wins).
inline std::vector<TempDisplayItem> parseDisplayList(const std::string& list)
{
    std::vector<TempDisplayItem> out;
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

        TempDisplayItem item;
        bool known = true;
        if (tok == "temp" || tok == "temperature")      item = TDI_TEMP;
        else if (tok == "humidity" || tok == "rh")      item = TDI_HUMIDITY;
        else if (tok == "pressure" || tok == "hpa")     item = TDI_PRESSURE;
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

#endif // TEMP_DISPLAY_HPP
