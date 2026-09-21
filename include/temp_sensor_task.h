#pragma once

// FreeRTOS task that periodically reads a TempSensor and scrolls the result
// on the LED matrix display.
//
// The task parameter must be a TempSensor* (passed as void*).
// Example:
//   TempSensor* sensor = new StubTempSensor();
//   xTaskCreate(temp_sensor_task, "TempSensor", 4096, sensor, 1, nullptr);
void temp_sensor_task(void* parameter);
