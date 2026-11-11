#pragma once

// Open-Meteo reports the weather as a WMO code (0..99). This maps it onto the
// OpenWeatherMap condition id the rest of the firmware already understands
// (icons via weatherIconIndex(), the weather_id placeholder) and gives a short
// description in the display language. Pure, host-tested in
// tests/host/weather_wmo_check.cpp. ASCII only: the matrix font has no
// diacritics.

#include <display_strings.hpp>

// Closest OWM id for a WMO code; 804 (overcast) for anything unknown.
inline int wmo_to_owm(int wmo)
{
    switch (wmo) {
        case 0:  return 800;            // clear sky
        case 1:  return 801;            // mainly clear
        case 2:  return 802;            // partly cloudy
        case 3:  return 804;            // overcast
        case 45: return 741;            // fog
        case 48: return 741;            // depositing rime fog
        case 51: return 300;            // drizzle: light
        case 53: return 301;            //          moderate
        case 55: return 302;            //          dense
        case 56: case 57: return 511;   // freezing drizzle
        case 61: return 500;            // rain: slight
        case 63: return 501;            //       moderate
        case 65: return 502;            //       heavy
        case 66: case 67: return 511;   // freezing rain
        case 71: return 600;            // snow: slight
        case 73: return 601;            //       moderate
        case 75: return 602;            //       heavy
        case 77: return 600;            // snow grains
        case 80: return 520;            // rain showers: slight
        case 81: return 521;            //               moderate
        case 82: return 522;            //               violent
        case 85: return 620;            // snow showers: slight
        case 86: return 621;            //               heavy
        case 95: return 211;            // thunderstorm
        case 96: case 99: return 202;   // thunderstorm with hail
        default: return 804;
    }
}

enum class WmoGroup {
    Clear, MainlyClear, PartlyCloudy, Overcast, Fog, Drizzle, FreezingDrizzle,
    Rain, FreezingRain, Snow, RainShowers, SnowShowers, Thunderstorm, Count
};

inline WmoGroup wmo_group(int wmo)
{
    if (wmo == 0) return WmoGroup::Clear;
    if (wmo == 1) return WmoGroup::MainlyClear;
    if (wmo == 2) return WmoGroup::PartlyCloudy;
    if (wmo == 45 || wmo == 48) return WmoGroup::Fog;
    if (wmo >= 51 && wmo <= 55) return WmoGroup::Drizzle;
    if (wmo == 56 || wmo == 57) return WmoGroup::FreezingDrizzle;
    if (wmo >= 61 && wmo <= 65) return WmoGroup::Rain;
    if (wmo == 66 || wmo == 67) return WmoGroup::FreezingRain;
    if (wmo >= 71 && wmo <= 77) return WmoGroup::Snow;
    if (wmo >= 80 && wmo <= 82) return WmoGroup::RainShowers;
    if (wmo == 85 || wmo == 86) return WmoGroup::SnowShowers;
    if (wmo >= 95 && wmo <= 99) return WmoGroup::Thunderstorm;
    return WmoGroup::Overcast;   // 3 and anything unknown
}

inline const char* wmo_description(int wmo, Lang l)
{
    static const char* const kText[][(int)WmoGroup::Count] = {
        // En
        {"clear sky", "mainly clear", "partly cloudy", "overcast", "fog", "drizzle",
         "freezing drizzle", "rain", "freezing rain", "snow", "rain showers",
         "snow showers", "thunderstorm"},
        // Fr
        {"ciel degage", "plutot degage", "partiellement nuageux", "couvert", "brouillard",
         "bruine", "bruine verglacante", "pluie", "pluie verglacante", "neige",
         "averses", "averses de neige", "orage"},
        // Pl
        {"bezchmurnie", "przewaznie pogodnie", "czesciowe zachmurzenie", "pochmurno",
         "mgla", "mzawka", "marznaca mzawka", "deszcz", "marznacy deszcz", "snieg",
         "przelotny deszcz", "przelotny snieg", "burza"},
        // It
        {"sereno", "poco nuvoloso", "parzialmente nuvoloso", "coperto", "nebbia",
         "pioggerella", "pioggerella gelata", "pioggia", "pioggia gelata", "neve",
         "rovesci", "rovesci di neve", "temporale"},
        // Es
        {"despejado", "mayormente despejado", "parcialmente nublado", "cubierto",
         "niebla", "llovizna", "llovizna helada", "lluvia", "lluvia helada", "nieve",
         "chubascos", "chubascos de nieve", "tormenta"},
        // Pt
        {"ceu limpo", "pouco nublado", "parcialmente nublado", "encoberto", "nevoeiro",
         "chuvisco", "chuvisco gelado", "chuva", "chuva gelada", "neve", "aguaceiros",
         "aguaceiros de neve", "trovoada"},
        // De
        {"klar", "ueberwiegend klar", "teilweise bewoelkt", "bedeckt", "Nebel",
         "Nieselregen", "gefrierender Niesel", "Regen", "gefrierender Regen", "Schnee",
         "Regenschauer", "Schneeschauer", "Gewitter"},
    };
    return kText[(int)l][(int)wmo_group(wmo)];
}
