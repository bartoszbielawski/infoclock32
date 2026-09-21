#pragma once

#include <cstdio>
#include <cmath>
#include <string>

// Parse a string into a numeric value, returning default_value on failure.
// Use NAN as default_value for floats when you need to distinguish
// a failed parse from a legitimate 0.0 result.

inline int parse_value(const std::string& s, int default_value)
{
    int result;
    return sscanf(s.c_str(), "%d", &result) == 1 ? result : default_value;
}

inline long parse_value(const std::string& s, long default_value)
{
    long result;
    return sscanf(s.c_str(), "%ld", &result) == 1 ? result : default_value;
}

inline float parse_value(const std::string& s, float default_value)
{
    float result;
    return sscanf(s.c_str(), "%f", &result) == 1 ? result : default_value;
}
