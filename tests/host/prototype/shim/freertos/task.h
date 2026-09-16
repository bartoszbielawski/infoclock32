#pragma once
// Host shim: FreeRTOS task API.
#include <FreeRTOS.h>

typedef void (*TaskFunction_t)(void*);
typedef void* TaskHandle_t;

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stackDepth,
                       void* parameter, UBaseType_t priority, TaskHandle_t* createdTask);

void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle();
const char* pcTaskGetName(TaskHandle_t task);
UBaseType_t uxTaskGetNumberOfTasks();

// Task notifications (the ResourceManager grant/release handshake).
uint32_t ulTaskNotifyTake(BaseType_t clearCountOnExit, TickType_t ticks);
void xTaskNotifyGive(TaskHandle_t task);

// Critical sections: a single recursive lock is plenty for a logic prototype.
void host_critical_enter();
void host_critical_exit();
#define taskENTER_CRITICAL(...) host_critical_enter()
#define taskEXIT_CRITICAL(...)  host_critical_exit()

// Prototype extension: force the named task to hang after 20 s of uptime —
// used to demo the watchdog supervisor without hardware.
void host_set_hang(const char* taskName);
