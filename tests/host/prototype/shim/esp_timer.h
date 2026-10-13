#pragma once
// Host shim: ESP-IDF esp_timer bits used by the firmware (esp_timer_get_time).
// On the device this returns microseconds since boot; on the host we map it to
// the steady clock so uptime-style deltas in the logs are monotonic.
#include <stdint.h>

int64_t esp_timer_get_time();