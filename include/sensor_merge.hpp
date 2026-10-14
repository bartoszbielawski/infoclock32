#pragma once

// Pure policy for the multi-sensor round (no Arduino / FreeRTOS / DataStore),
// so it is host-testable — see tests/host/sensor_merge_check.cpp.
//
//  * pruneSensors()     drops sensors that failed begin() and stub entries
//                       that would only ever fail read().
//  * firstProvider()    "first listed provider wins": the sensor earliest in
//                       the `sensors` list that offers a measurement owns it.
//  * slotAccepts()      a non-owner may only write a measurement while the
//                       owner's value is missing or stale (owner is dead).
//  * slotStale()        a value older than staleAfterSeconds() is dropped so a
//                       dead sensor does not keep publishing its last reading.
//  * wakeDelayMs()      sleep until the next poll/scroll is due (capped), so
//                       sensor_interval is not quantised to the loop period.
//
// All times are monotonic seconds (int64) — never wall-clock, so an NTP step
// can't make a reading look ancient or future.

#include <cstdint>
#include <vector>
#include <sensor.hpp>

enum Measurement
{
    MEAS_TEMP = 0,
    MEAS_HUMIDITY,
    MEAS_PRESSURE,
    MEAS_CO2,
    MEAS_COUNT
};

inline bool sensorProvides(const Sensor& s, Measurement m)
{
    switch (m)
    {
        case MEAS_TEMP:     return true;
        case MEAS_HUMIDITY: return s.hasHumidity();
        case MEAS_PRESSURE: return s.hasPressure();
        case MEAS_CO2:      return s.hasCo2();
        default:            return false;
    }
}

// Index of the first sensor providing `m`, or -1.
inline int firstProvider(const std::vector<Sensor*>& list, Measurement m)
{
    for (size_t i = 0; i < list.size(); i++)
        if (sensorProvides(*list[i], m)) return (int)i;
    return -1;
}

// Remove (and delete) sensors that are not worth polling: those whose begin()
// failed (initOk[i] == false) and StubSensors when any real sensor remains.
// If only stubs remain, the list is emptied so the task can exit instead of
// logging a failed read forever. initOk must be parallel to `list`.
inline void pruneSensors(std::vector<Sensor*>& list, const std::vector<bool>& initOk)
{
    bool anyReal = false;
    for (size_t i = 0; i < list.size(); i++)
        if (i < initOk.size() && initOk[i] && !list[i]->isStub()) anyReal = true;

    std::vector<Sensor*> kept;
    for (size_t i = 0; i < list.size(); i++)
    {
        bool ok = i < initOk.size() && initOk[i];
        if (ok && anyReal && !list[i]->isStub())
            kept.push_back(list[i]);
        else
            delete list[i];
    }
    list.swap(kept);
}

// Latest accepted value of one measurement and who wrote it.
struct MeasurementSlot
{
    bool    have   = false;
    int     source = -1;     // sensor index that wrote it
    int64_t stamp  = 0;      // monotonic seconds of the write
};

// A value is stale once it is older than 3 full rounds (never under 2 min), so
// a single slow or failed read does not blank a reading.
inline int64_t staleAfterSeconds(size_t sensorCount, int intervalSeconds)
{
    int64_t s = (int64_t)3 * (int64_t)(sensorCount ? sensorCount : 1) * intervalSeconds;
    return s < 120 ? 120 : s;
}

inline bool slotStale(const MeasurementSlot& slot, int64_t now, int64_t staleAfter)
{
    return slot.have && (now - slot.stamp) > staleAfter;
}

inline bool slotFresh(const MeasurementSlot& slot, int64_t now, int64_t staleAfter)
{
    return slot.have && !slotStale(slot, now, staleAfter);
}

// Should sensor `sensorIdx` (reading OK at `now`) overwrite this slot?
// `owner` is firstProvider() for the measurement.
inline bool slotAccepts(const MeasurementSlot& slot, int sensorIdx, int owner,
                        int64_t now, int64_t staleAfter)
{
    if (sensorIdx == owner)       return true;   // the primary provider
    if (!slot.have)               return true;   // nobody has written yet
    if (slot.source == sensorIdx) return true;   // keep our own fallback fresh
    return (now - slot.stamp) > staleAfter;      // owner has gone quiet
}

// Milliseconds to sleep: until the nearer of the next poll / next scroll, but
// never less than 1 s (don't spin) nor more than capMs (heartbeat cadence).
// Pass a negative secsUntilShow when scrolling is not time-gated.
inline uint32_t wakeDelayMs(int64_t secsUntilPoll, int64_t secsUntilShow,
                            uint32_t capMs = 30000)
{
    int64_t s = secsUntilPoll;
    if (secsUntilShow >= 0 && secsUntilShow < s) s = secsUntilShow;
    if (s < 1) s = 1;
    int64_t ms = s * 1000;
    return ms > (int64_t)capMs ? capMs : (uint32_t)ms;
}
