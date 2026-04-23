#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <pins.hpp>
#include <task_registry.hpp>

static const int LED = LED_BLINK_PIN;

void blink_led_task(void *pvParameter) {
    registerTask("BlinkLED");
  pinMode(LED, OUTPUT);
  while (1) {
    digitalWrite(LED, HIGH);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    digitalWrite(LED, LOW);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}