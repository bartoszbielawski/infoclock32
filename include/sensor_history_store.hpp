#pragma once

// Device-side owner of the 24 h sensor history (see sensor_history.hpp):
// the sensor task offers a sample on every reading, the web server copies a
// snapshot for /api/history. A mutex guards the ring; the copy is a plain
// memcpy-sized loop, so the sensor task never waits on the network.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sensor_history.hpp>

class SensorHistoryStore {
public:
    static SensorHistoryStore& getInstance()
    {
        static SensorHistoryStore inst;
        return inst;
    }

    bool offer(const HistorySample& s, uint32_t stepS)
    {
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
        const bool stored = history_.offer(s, stepS);
        xSemaphoreGive(mutex_);
        return stored;
    }

    // Copy up to `cap` samples, oldest first; returns how many were copied.
    size_t snapshot(HistorySample* out, size_t cap)
    {
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) return 0;
        size_t n = history_.size() < cap ? history_.size() : cap;
        for (size_t i = 0; i < n; i++) out[i] = history_.at(i);
        xSemaphoreGive(mutex_);
        return n;
    }

private:
    SensorHistoryStore() : mutex_(xSemaphoreCreateMutex()) {}
    SensorHistory history_;
    SemaphoreHandle_t mutex_;
};

// Bucket length in seconds from config key history_step_min (1..60, default 5).
uint32_t history_step_s();
