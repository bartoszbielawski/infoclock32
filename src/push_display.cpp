#include <Arduino.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <logger.hpp>
#include <task_registry.hpp>
#include <status_task.h>
#include <push_display.h>

namespace {

struct PushItem
{
    char* text;    // malloc'd, owned by the queue until scrolled
    int   speedMs;
};

constexpr UBaseType_t kQueueDepth   = 4;
constexpr uint32_t    kTaskStack    = 4096;
constexpr uint32_t    kAcquireMs    = 30000;   // one try; the clock yields in 7-9 s
constexpr int         kMaxTries     = 4;       // then the message is dropped

QueueHandle_t     s_queue = nullptr;
std::atomic<bool> s_running{false};

void push_task(void*);

void ensure_task()
{
    bool expected = false;
    if (!s_running.compare_exchange_strong(expected, true))
        return;   // already running; it drains the queue
    if (xTaskCreate(push_task, "PushTask", kTaskStack, nullptr, 1, nullptr) != pdPASS)
    {
        s_running = false;   // messages stay queued; the next push retries
        logPrintf("DISP", "push task could not start (heap %u)",
                  (unsigned)esp_get_free_heap_size());
    }
}

void push_task(void*)
{
    registerTask("Push", kTaskStack, 120000);
    auto& rmd = ResourceManager<LMDS>::getInstance();
    int tries = 0;
    for (;;)
    {
        task_heartbeat();
        PushItem item;
        // A timed status owns the display: leave the messages queued and exit;
        // the status task kicks us again when it ends.
        if (!status_info().active && xQueuePeek(s_queue, &item, 0) == pdTRUE)
        {
            task_heartbeat_grace(kAcquireMs + 90000);   // wait + scroll
            if (auto display = rmd.acquire(pdMS_TO_TICKS(kAcquireMs), true))
            {
                xQueueReceive(s_queue, &item, 0);   // only this task receives
                scrollMessage(std::string(item.text), display, item.speedMs);
                free(item.text);
                tries = 0;
            }
            else if (++tries >= kMaxTries)
            {
                xQueueReceive(s_queue, &item, 0);
                logPrintf("DISP", "push dropped, display busy: %.32s", item.text);
                free(item.text);
                tries = 0;
            }
            continue;
        }
        // Nothing to do (or a status is up): exit. Same race-safe pattern as
        // the status task - drop the flag, then look again.
        task_mark_exited();
        s_running = false;
        bool expected = false;
        if (!status_info().active && uxQueueMessagesWaiting(s_queue) > 0 &&
            s_running.compare_exchange_strong(expected, true))
        {
            registerTask("Push", kTaskStack, 120000);
            continue;
        }
        vTaskDelete(nullptr);
    }
}

} // namespace

void push_display_init()
{
    if (!s_queue)
        s_queue = xQueueCreate(kQueueDepth, sizeof(PushItem));
}

bool show_once(const std::string& text, int speedMs)
{
    if (!s_queue || text.empty())
        return false;
    char* copy = static_cast<char*>(malloc(text.size() + 1));
    if (!copy)
        return false;
    memcpy(copy, text.c_str(), text.size() + 1);
    PushItem item{copy, speedMs};
    if (xQueueSend(s_queue, &item, 0) != pdTRUE)
    {
        free(copy);
        logPrintf("DISP", "push queue full, message dropped");
        return false;
    }
    if (!status_info().active)
        ensure_task();
    return true;
}

void push_display_kick()
{
    if (s_queue && uxQueueMessagesWaiting(s_queue) > 0)
        ensure_task();
}
