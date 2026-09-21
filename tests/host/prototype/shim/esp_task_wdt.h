#pragma once
// Host shim: the hardware task watchdog does not exist on the host. The
// software supervisor (watchdog_task) does all the work, exactly like on
// device where it feeds the TWDT.
#include <stdint.h>

inline int esp_task_wdt_add(void*) { return 0; }
inline int esp_task_wdt_reset() { return 0; }
