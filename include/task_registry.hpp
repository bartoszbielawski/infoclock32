#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <watchdog_utils.hpp>

static const int kTaskRegistryMax = 24;

struct RegisteredTask {
    char name[configMAX_TASK_NAME_LEN + 4];
    TaskHandle_t handle;
    uint32_t totalStack;

    // Watchdog bookkeeping (supervisor lives in watchdog_task.cpp).
    // timeoutMs == 0 → not watched. lastSeenMs anchors the current interval,
    // graceMs extends it across planned long sleeps/waits, warned implements
    // warn-once enforcement, exited marks tasks that ended themselves.
    uint32_t timeoutMs  = 0;
    uint32_t lastSeenMs = 0;
    uint32_t graceMs    = 0;
    bool     warned     = false;
    bool     exited     = false;
};

class TaskRegistry {
public:
    enum class Verdict { Healthy, Warn, Reboot };

    static TaskRegistry& getInstance() {
        static TaskRegistry inst;
        return inst;
    }

    void add(const char* displayName, uint32_t totalStack, uint32_t watchdogTimeoutMs = 0) {
        uint32_t now = millis();
        taskENTER_CRITICAL(&mux_);
        if (count_ < kTaskRegistryMax) {
            RegisteredTask& t = tasks_[count_];
            strlcpy(t.name, displayName, sizeof(t.name));
            t.handle     = xTaskGetCurrentTaskHandle();
            t.totalStack = totalStack;
            t.timeoutMs  = watchdogTimeoutMs;
            t.lastSeenMs = now;
            t.graceMs    = 0;
            t.warned     = false;
            t.exited     = false;
            count_++;
        }
        taskEXIT_CRITICAL(&mux_);
    }

    // Refresh the caller's heartbeat; ends any grace window and any warn state.
    void beat() {
        uint32_t now = millis();
        taskENTER_CRITICAL(&mux_);
        RegisteredTask* t = findLocked(xTaskGetCurrentTaskHandle());
        if (t) { t->lastSeenMs = now; t->graceMs = 0; t->warned = false; }
        taskEXIT_CRITICAL(&mux_);
    }

    // Announce a planned sleep/wait of up to `ms` starting now (cleared on the
    // next beat). Only raises the allowance, never lowers it.
    void extend(uint32_t ms) {
        taskENTER_CRITICAL(&mux_);
        RegisteredTask* t = findLocked(xTaskGetCurrentTaskHandle());
        if (t && ms > t->graceMs) t->graceMs = ms;
        taskEXIT_CRITICAL(&mux_);
    }

    // Broadcast grace: the caller is about to monopolise a shared resource
    // (the display) for up to `ms`, which blocks everyone waiting on it.
    // Warn state is deliberately preserved — only a real beat proves liveness.
    void extendAll(uint32_t ms) {
        taskENTER_CRITICAL(&mux_);
        for (int i = 0; i < count_; i++) {
            RegisteredTask& t = tasks_[i];
            if (t.timeoutMs && !t.exited && ms > t.graceMs) t.graceMs = ms;
        }
        taskEXIT_CRITICAL(&mux_);
    }

    // The caller's task is about to delete itself — stop watching it.
    void markExited() {
        taskENTER_CRITICAL(&mux_);
        RegisteredTask* t = findLocked(xTaskGetCurrentTaskHandle());
        if (t) t->exited = true;
        taskEXIT_CRITICAL(&mux_);
    }

    // Supervisor: classify the first stale watched task found.
    // Warn: first stale check for that task (warn state is set).
    // Reboot: stale again while still warned.
    // Healthy: nothing stale — note a previously warned task is only forgiven
    // by an actual beat(), not merely by falling back under its deadline.
    Verdict evaluate(uint32_t nowMs, const char** nameOut) {
        Verdict verdict = Verdict::Healthy;
        taskENTER_CRITICAL(&mux_);
        for (int i = 0; i < count_; i++) {
            RegisteredTask& t = tasks_[i];
            if (!t.timeoutMs || t.exited) continue;
            if (!wdt_is_stale(nowMs, t.lastSeenMs, t.graceMs, t.timeoutMs)) continue;
            *nameOut = t.name;
            if (t.warned)
                verdict = Verdict::Reboot;
            else {
                t.warned = true;
                verdict  = Verdict::Warn;
            }
            break;
        }
        taskEXIT_CRITICAL(&mux_);
        return verdict;
    }

    int count() const { return count_; }
    const RegisteredTask& get(int i) const { return tasks_[i]; }

private:
    RegisteredTask* findLocked(TaskHandle_t handle) {
        for (int i = 0; i < count_; i++)
            if (tasks_[i].handle == handle) return &tasks_[i];
        return nullptr;
    }

    RegisteredTask tasks_[kTaskRegistryMax] = {};
    int count_ = 0;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

inline void registerTask(const char* name, uint32_t totalStack, uint32_t watchdogTimeoutMs = 0) {
    TaskRegistry::getInstance().add(name, totalStack, watchdogTimeoutMs);
}

// ── watchdog helpers, called from inside the watched task ────────────────────

// One loop iteration completed.
inline void task_heartbeat() { TaskRegistry::getInstance().beat(); }

// A planned long sleep/wait of up to `ms` starts now.
inline void task_heartbeat_grace(uint32_t ms) { TaskRegistry::getInstance().extend(ms); }

// The caller is about to block everyone else on a shared resource for up to `ms`.
inline void task_grace_all(uint32_t ms) { TaskRegistry::getInstance().extendAll(ms); }

// The caller is about to delete its own task.
inline void task_mark_exited() { TaskRegistry::getInstance().markExited(); }
