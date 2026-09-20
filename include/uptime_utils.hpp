#pragma once
#include <stdio.h>
#include <stddef.h>

// Single source of truth for uptime rendering.  Used by the web UI, the MQTT
// heartbeat and DeviceStore so every surface reports the same string.
//
// Format: "2d 4h 9m 31s"; the day field is omitted while uptime is below a day.
// Hours wrap at 24 once the day field appears, so no field ever runs unbounded.
inline void format_uptime(char* buf, size_t buf_sz, unsigned long ms)
{
    unsigned long days = ms / 86400000UL;
    unsigned long hrs  = (ms / 3600000UL) % 24UL;
    unsigned long mins = (ms / 60000UL)   % 60UL;
    unsigned long secs = (ms / 1000UL)    % 60UL;

    if (days > 0)
        snprintf(buf, buf_sz, "%lud %luh %lum %lus", days, hrs, mins, secs);
    else
        snprintf(buf, buf_sz, "%luh %lum %lus", hrs, mins, secs);
}
