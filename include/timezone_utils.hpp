#pragma once
#include <cstdlib>   // setenv
#include <ctime>     // tzset
#include <string>
#include <data_store.hpp>

// Apply the timezone stored in DataStore key "timezone" (POSIX TZ string).
// Falls back to "UTC0" if the key is absent or empty.
// Must be called after DataStore::load_from_file().
// Safe to call any time the setting changes — affects all subsequent localtime() calls.
inline void apply_timezone()
{
    std::string tz = DataStore::getInstance().get_value("timezone", "UTC0");
    if (tz.empty()) tz = "UTC0";
    setenv("TZ", tz.c_str(), 1);
    tzset();
}
