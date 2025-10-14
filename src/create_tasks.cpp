
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void blink_led_task(void *pvParameter);

void create_tasks() {
  xTaskCreate(
      blink_led_task,   // Task function
      "Blink LED Task", // Name of the task
      1024,            // Stack size (in words, not bytes)
      NULL,            // Task input parameter
      1,               // Priority of the task
      NULL             // Task handle
  );
}   