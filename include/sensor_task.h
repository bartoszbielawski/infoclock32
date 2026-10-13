#pragma once

// FreeRTOS task that periodically reads a Sensor and scrolls the result
// on the LED matrix display.
//
// The task parameter must be a Sensor* (passed as void*).
// Example:
//   Sensor* sensor = new StubSensor();
//   xTaskCreate(sensor_task, "SensorTask", 4096, sensor, 1, nullptr);
void sensor_task(void* parameter);
