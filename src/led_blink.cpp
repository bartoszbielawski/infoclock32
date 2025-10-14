#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>

static const int LED = 8;

void blink_led_task(void *pvParameter) {
  pinMode(LED, OUTPUT);
  while (1) {
    digitalWrite(LED, HIGH);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    digitalWrite(LED, LOW);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}