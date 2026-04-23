#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const int kTaskRegistryMax = 24;

struct RegisteredTask {
    char name[configMAX_TASK_NAME_LEN + 4];
    TaskHandle_t handle;
    uint32_t totalStack;
};

class TaskRegistry {
public:
    static TaskRegistry& getInstance() {
        static TaskRegistry inst;
        return inst;
    }

    void add(const char* displayName, uint32_t totalStack) {
        taskENTER_CRITICAL(&mux_);
        if (count_ < kTaskRegistryMax) {
            strlcpy(tasks_[count_].name, displayName, sizeof(tasks_[0].name));
            tasks_[count_].handle     = xTaskGetCurrentTaskHandle();
            tasks_[count_].totalStack = totalStack;
            count_++;
        }
        taskEXIT_CRITICAL(&mux_);
    }

    int count() const { return count_; }
    const RegisteredTask& get(int i) const { return tasks_[i]; }

private:
    RegisteredTask tasks_[kTaskRegistryMax] = {};
    int count_ = 0;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

inline void registerTask(const char* name, uint32_t totalStack) {
    TaskRegistry::getInstance().add(name, totalStack);
}
