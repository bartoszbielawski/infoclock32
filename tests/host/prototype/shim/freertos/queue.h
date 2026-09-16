#pragma once
// Host shim: FreeRTOS queues.
#include <FreeRTOS.h>

typedef void* QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void* out, TickType_t ticks);
