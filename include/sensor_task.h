#pragma once

// FreeRTOS task that polls a round-robin list of Sensors and scrolls the
// consolidated readings on the LED matrix display. All sensors share the same
// flat RuntimeStore keys (temp_c / temp_rh / temp_hpa / temp_co2). For each
// measurement the first listed sensor that provides it owns the key; a later
// sensor only fills in while the owner is stale (see include/sensor_merge.hpp).
// Sensors that fail begin() are dropped from the round, stubs are dropped when
// a real sensor exists, and readings with no update for 3 rounds (min 2 min)
// are removed from the store so dead sensors don't report stale data.
//
// The task parameter must be a heap-allocated std::vector<Sensor*> (passed as
// void*), built by createSensorList() in include/sensor_factory.hpp. The task
// owns the vector and deletes every sensor on exit.
// Example:
//   auto* sensors = new std::vector<Sensor*>(createSensorList());
//   xTaskCreate(sensor_task, "SensorTask", 4096, sensors, 1, nullptr);
void sensor_task(void* parameter);
