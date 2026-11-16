#pragma once
// Download name for the /config.txt backup (GET /config/download).
// Pure, host-tested in tests/host/config_backup_check.cpp.
//
//   "<hostname>-config-YYYY-MM-DD.txt"   wall clock synced
//   "<hostname>-config.txt"              clock not set yet (no 1970 dates)
//
// The hostname is reduced to letters, digits and '-' so it is always a safe
// file name and Content-Disposition value; empty falls back to infoclock32.
#include <cstdio>
#include <ctime>
#include <string>

inline std::string configBackupFilename(const std::string& hostname, time_t now, bool timeValid)
{
    std::string name;
    for (char c : hostname)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')
            name += c;
    if (name.size() > 32) name.resize(32);
    if (name.empty()) name = "infoclock32";

    name += "-config";
    if (timeValid)
    {
        struct tm t;
        localtime_r(&now, &t);
        char date[40];   // room for any int, so GCC sees no truncation
        snprintf(date, sizeof(date), "-%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        name += date;
    }
    return name + ".txt";
}
