#pragma once

#include <stdint.h>

// Serial console + night mode + Wi-Fi monitor in one task
// (src/housekeeping_task.cpp). Started before hardware_init(), so the serial
// console works while the setup portal blocks setup().
static constexpr uint32_t kHousekeepingStack = 4096;
void housekeeping_task(void* parameter);
