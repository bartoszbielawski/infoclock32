#pragma once
// Host shim: FreeRTOS semaphores.
#include <FreeRTOS.h>

typedef void* SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex();
void xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
void vSemaphoreDelete(SemaphoreHandle_t sem);
