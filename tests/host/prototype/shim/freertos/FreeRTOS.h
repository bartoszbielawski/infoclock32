#pragma once
// Host shim: FreeRTOS types and macros used by the firmware.
#include <stdint.h>
#include <stddef.h>

typedef int32_t      BaseType_t;
typedef uint32_t     UBaseType_t;
typedef uint32_t     TickType_t;
typedef unsigned int uint32_t_hack_unused;

#define pdTRUE    1
#define pdFALSE   0
#define pdPASS    1
#define pdFAIL    0

#define portMAX_DELAY       0xFFFFFFFFu
#define portTICK_PERIOD_MS  1u
#define pdMS_TO_TICKS(ms)   ((TickType_t)(ms))
#define configMAX_TASK_NAME_LEN 16

typedef uint32_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0

#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
