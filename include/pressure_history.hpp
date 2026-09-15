#pragma once
#ifndef PRESSURE_HISTORY_HPP
#define PRESSURE_HISTORY_HPP

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <pressure_trend.hpp>

// Mutex-protected singleton holding recent pressure readings for trend
// computation. temp_sensor_task writes a sample on every successful read;
// the same task reads the trend back to pick a display glyph, and publishes
// pressure_trend / pressure_rate placeholders to RuntimeStore.
class PressureHistoryStore
{
public:
    static PressureHistoryStore& getInstance()
    {
        static PressureHistoryStore instance;
        return instance;
    }

    PressureHistoryStore(const PressureHistoryStore&) = delete;
    PressureHistoryStore& operator=(const PressureHistoryStore&) = delete;

    void add(time_t t, float hpa)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        ring_.add(t, hpa);
        xSemaphoreGive(mutex_);
    }

    // True (with kind and slope filled in) when enough fresh samples exist.
    bool trend(time_t now, TrendKind& kind, float& hpaPerHour)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        bool ok = ring_.trend(now, hpaPerHour);
        if (ok)
            kind = classifyPressureTrend(hpaPerHour);
        xSemaphoreGive(mutex_);
        return ok;
    }

private:
    PressureHistoryStore() { mutex_ = xSemaphoreCreateMutex(); }
    ~PressureHistoryStore()
    {
        if (mutex_) vSemaphoreDelete(mutex_);
    }

    PressureRing<kPressureRingCapacity> ring_;
    SemaphoreHandle_t mutex_ = nullptr;
};

#endif // PRESSURE_HISTORY_HPP
