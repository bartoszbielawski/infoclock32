#include <Arduino.h>
#include <esp_attr.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <logger.hpp>
#include <task_registry.hpp>
#include <watchdog_task.h>

static const uint32_t WDT_CULPRIT_MAGIC = 0x57445431;  // "WDT1"

// RTC memory survives esp_restart() but its content is undefined after a
// power-on, so the magic doubles as a validity check.
RTC_NOINIT_ATTR static uint32_t s_culpritMagic;
RTC_NOINIT_ATTR static char     s_culpritName[24];

void wdt_store_culprit(const char* name)
{
    strlcpy(s_culpritName, name, sizeof(s_culpritName));
    s_culpritMagic = WDT_CULPRIT_MAGIC;
}

bool wdt_take_culprit(char* out, size_t len)
{
    if (s_culpritMagic != WDT_CULPRIT_MAGIC) return false;
    strlcpy(out, s_culpritName, len);
    s_culpritMagic = 0;   // consume: only the first boot after a watchdog restart reports it
    return true;
}

static void watchdog_task(void*)
{
    registerTask("Watchdog", 3072);
    // The supervisor guards itself with the hardware task watchdog: if it ever
    // wedges, it stops feeding and the TWDT panics → reboot.
    esp_task_wdt_add(nullptr);

    logPrintf("WDT", "supervisor started, %d tasks registered",
              TaskRegistry::getInstance().count());

    while (true)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        const char* name = nullptr;
        auto verdict = TaskRegistry::getInstance().evaluate(millis(), &name);

        switch (verdict)
        {
        case TaskRegistry::Verdict::Healthy:
            esp_task_wdt_reset();
            break;

        case TaskRegistry::Verdict::Warn:
            logPrintf("WDT", "%s stale — warning (reboot on next stale check)", name);
            esp_task_wdt_reset();
            break;

        case TaskRegistry::Verdict::Reboot:
            wdt_store_culprit(name);   // before logging: survives even a wedged logger
            logPrintf("WDT", "%s stale twice — rebooting", name);
            vTaskDelay(250 / portTICK_PERIOD_MS);   // let syslog drain
            esp_restart();
            break;
        }
    }
}

void start_watchdog_task()
{
    xTaskCreate(watchdog_task, "Watchdog", 3072, nullptr, 3, nullptr);
}
