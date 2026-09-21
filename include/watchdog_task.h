#pragma once
#include <cstddef>

// Watchdog supervisor (implementation in watchdog_task.cpp).
//
// Watches every task that registered with a non-zero timeout in TaskRegistry
// (third argument of registerTask). Enforcement is warn-once: the first stale
// check for a task only logs a warning, the second consecutive one reboots
// the device. The supervisor itself is watched by the hardware task watchdog
// (TWDT, 5 s, panic mode), so a wedged supervisor still leads to a reboot.

// Persist the culprit so the boot banner can show it after the restart.
void wdt_store_culprit(const char* name);

// Returns true if the previous restart was watchdog-triggered; fills `out`
// with the hung task's name and consumes the stored record.
bool wdt_take_culprit(char* out, size_t len);

// Creates the supervisor task. Call once from setup(), after all other tasks.
void start_watchdog_task();
