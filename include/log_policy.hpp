#pragma once

// Which log lines go to the in-memory history (/log) and syslog
// (pure, host-tested in tests/host/log_policy_check.cpp).
//
// The display tasks log every frame they show (a clock line every few seconds,
// each scrolled message), and /log keeps only the last 40 lines: within about
// three minutes boot, Wi-Fi, OTA and error lines were pushed out. Display
// lines therefore stay on Serial only, unless `log_display=1`.

#include <cstring>

inline bool isDisplayLogTag(const char* tag)
{
    return tag && std::strcmp(tag, "DISP") == 0;
}

// True when the line should reach /log and syslog (Serial always gets it).
inline bool logLineKept(const char* tag, bool displayLoggingOn)
{
    return displayLoggingOn || !isDisplayLogTag(tag);
}
