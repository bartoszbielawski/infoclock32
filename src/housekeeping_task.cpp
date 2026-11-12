#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <task_registry.hpp>
#include <hardware_init.h>
#include <night_mode_task.h>
#include <serial_console_task.h>
#include <housekeeping_task.h>

// One task for the small periodic jobs that used to own a 4 KB stack each:
//   every 20 ms  serial console input
//   every 1 s    night mode / auto-brightness
//   every 5 s    Wi-Fi reconnect and fallback AP
// Each tick must return quickly; anything that waits for the display runs
// in a short-lived task of its own (see net_info_task in hardware_init.cpp).
void housekeeping_task(void* /*parameter*/)
{
    registerTask("Housekeeping", kHousekeepingStack, 90000);
    uint32_t lastSecondMs = millis();
    uint32_t lastWifiMs   = millis();
    for (;;)
    {
        task_heartbeat();
        serial_console_poll();
        const uint32_t now = millis();
        if (now - lastSecondMs >= 1000)
        {
            lastSecondMs = now;
            night_mode_tick();
        }
        if (now - lastWifiMs >= 5000)
        {
            lastWifiMs = now;
            wifi_monitor_tick();
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
