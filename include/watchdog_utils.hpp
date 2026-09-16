#pragma once
#include <cstdint>

// Wrap-safe staleness check for the task watchdog (see watchdog_task.cpp).
//
// Returns true when more than (graceMs + timeoutMs) milliseconds have passed
// since lastSeenMs. Unsigned subtraction keeps the comparison correct across
// the ~49.7-day wrap of millis().
//
// Exactly on the limit → not stale (strict >), so a timeout of N ms means
// "first warning after at least N ms of silence".
inline bool wdt_is_stale(uint32_t nowMs, uint32_t lastSeenMs,
                         uint32_t graceMs, uint32_t timeoutMs)
{
    return (nowMs - lastSeenMs) > (graceMs + timeoutMs);
}
