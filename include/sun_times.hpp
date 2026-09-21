#pragma once
#ifndef INFOCLOCK32_SUN_TIMES_HPP
#define INFOCLOCK32_SUN_TIMES_HPP

// Sunrise/sunset via the NOAA/Wikipedia "sunrise equation"
// (https://en.wikipedia.org/wiki/Sunrise_equation). Pure math, no network;
// accuracy is on the order of a minute at mid latitudes.
// Local-time conversion goes through localtime(), so the configured POSIX
// timezone (timezone_utils.hpp) and DST are handled for free.

#include <ctime>
#include <cmath>

struct SunTimes {
    enum class Kind { Normal, PolarDay, PolarNight };
    Kind kind = Kind::Normal;
    int  sunrise_min = -1;  // local minutes since midnight
    int  sunset_min  = -1;
};

// Days since 2000-01-01 12:00 UTC for the civil date (integer math).
inline long sun_days_since_2000(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    long greg = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045; // Julian Day Number (starts at noon)
    return greg - 2451545; // JD 2451545.0 == 2000-01-01 12:00 UTC
}

inline int sun_local_minutes(time_t utc_epoch)
{
    struct tm lt;
    localtime_r(&utc_epoch, &lt);
    return (lt.tm_hour * 60 + lt.tm_min + 1440) % 1440; // wrap extreme-longitude edge cases
}

// Civil date (year, month 1-12, day) + WGS84 position → local sunrise/sunset.
// A non-finite result or |cos(omega)| > 1 encodes polar day/night.
inline SunTimes compute_sun_times(double lat_deg, double lon_deg,
                                  int year, int month, int day)
{
    const double RAD = M_PI / 180.0;
    SunTimes out;

    // n* = days since 2000-01-01 for solar noon at this longitude (east of
    // Greenwich sees the sun cross the meridian earlier in UTC), plus the
    // fixed 2000-epoch phase constants from the reference algorithm.
    double n = (double)sun_days_since_2000(year, month, day) + 0.0009 - lon_deg / 360.0;
    double Jstar = 2451545.0009 + n;                                     // approx. Julian date of solar noon
    double M = fmod(357.5291 + 0.98560028 * n, 360.0);                   // mean anomaly, deg
    double C = 1.9148 * sin(M * RAD) + 0.0200 * sin(2 * M * RAD)
             + 0.0003 * sin(3 * M * RAD);                                // equation of center
    double lambda = fmod(M + C + 283.01, 360.0);                         // ecliptic longitude, deg
    double Jtransit = Jstar + 0.0053 * sin(M * RAD) - 0.0069 * sin(2 * lambda * RAD);

    double sinDec = sin(lambda * RAD) * sin(23.44 * RAD);                // solar declination
    double dec = asin(sinDec);
    double cosOmega = (sin(-0.833 * RAD) - sin(lat_deg * RAD) * sinDec)
                    / (cos(lat_deg * RAD) * cos(dec));                   // -0.833° ≈ refraction + solar radius

    if (cosOmega > 1.0)  { out.kind = SunTimes::Kind::PolarNight; return out; }
    if (cosOmega < -1.0) { out.kind = SunTimes::Kind::PolarDay;   return out; }

    double omega = acos(cosOmega) / RAD;                                 // deg
    double Jrise = Jtransit - omega / 360.0;
    double Jset  = Jtransit + omega / 360.0;

    auto epoch_of = [](double julian_date) -> time_t {
        return (time_t)lround((julian_date - 2440587.5) * 86400.0);      // JD → Unix epoch seconds
    };

    out.kind = SunTimes::Kind::Normal;
    out.sunrise_min = sun_local_minutes(epoch_of(Jrise));
    out.sunset_min  = sun_local_minutes(epoch_of(Jset));
    return out;
}

#endif // INFOCLOCK32_SUN_TIMES_HPP
