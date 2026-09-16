// Host shim implementation: FreeRTOS tasks, notifications, queues, semaphores
// mapped onto std::thread / std::mutex / std::condition_variable.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "Arduino.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <string>

extern std::string host_hang_task_name;   // defined at the bottom of this file

// ── tasks ────────────────────────────────────────────────────────────────────

struct HostTask
{
    std::string name;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    uint32_t notifyCount = 0;
    std::atomic<bool> exited{false};
};

static std::vector<std::shared_ptr<HostTask>> tasks;
static std::mutex tasksMutex;
static thread_local HostTask* t_current = nullptr;

static void sleepTicks(TickType_t ticks)
{
    if (ticks == portMAX_DELAY)
    {
        for (;;) std::this_thread::sleep_for(std::chrono::hours(24));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t /*stackDepth*/,
                       void* parameter, UBaseType_t /*priority*/, TaskHandle_t* createdTask)
{
    auto ctx = std::make_shared<HostTask>();
    ctx->name = name ? name : "task";

    ctx->thread = std::thread([ctx, fn, parameter]() {
        t_current = ctx.get();
        fn(parameter);
        ctx->exited = true;
    });

    if (createdTask) *createdTask = (TaskHandle_t)ctx.get();

    std::lock_guard<std::mutex> lock(tasksMutex);
    tasks.push_back(ctx);
    return pdTRUE;
}

void vTaskDelay(TickType_t ticks)
{
    // Watchdog demo hook: the named task stops making progress after 20 s.
    if (t_current && !t_current->exited && host_hang_task_name == t_current->name &&
        millis() > 20000)
    {
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::hours(24));
        }
    }
    sleepTicks(ticks);
}

void vTaskDelete(TaskHandle_t task)
{
    HostTask* ctx = task ? (HostTask*)task : t_current;
    if (ctx)
    {
        ctx->exited = true;
        if (!task)
        {
            // self-delete: detach and park this thread forever
            for (auto& t : tasks)
                if (t.get() == ctx && t->thread.joinable())
                    t->thread.detach();
            for (;;)
                std::this_thread::sleep_for(std::chrono::hours(24));
        }
    }
}

TaskHandle_t xTaskGetCurrentTaskHandle() { return (TaskHandle_t)t_current; }

const char* pcTaskGetName(TaskHandle_t task)
{
    HostTask* ctx = task ? (HostTask*)task : t_current;
    return ctx ? ctx->name.c_str() : "main";
}

UBaseType_t uxTaskGetNumberOfTasks()
{
    std::lock_guard<std::mutex> lock(tasksMutex);
    UBaseType_t n = 0;
    for (auto& t : tasks)
        if (!t->exited) n++;
    return n;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t)
{
    // No real stacks on the host — report a healthy constant for the status page.
    return 2048;
}

// ── task notifications ───────────────────────────────────────────────────────

uint32_t ulTaskNotifyTake(BaseType_t clearCountOnExit, TickType_t ticks)
{
    if (!t_current) return 0;
    std::unique_lock<std::mutex> lock(t_current->mutex);
    if (t_current->notifyCount == 0)
    {
        if (ticks == portMAX_DELAY)
            t_current->cv.wait(lock, [&] { return t_current->notifyCount > 0; });
        else if (!t_current->cv.wait_for(lock, std::chrono::milliseconds(ticks),
                                         [&] { return t_current->notifyCount > 0; }))
            return 0;
    }
    uint32_t value = t_current->notifyCount;
    if (clearCountOnExit) t_current->notifyCount = 0;
    return value;
}

void xTaskNotifyGive(TaskHandle_t task)
{
    HostTask* ctx = (HostTask*)task;
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->notifyCount++;
    ctx->cv.notify_all();
}

// ── critical sections ────────────────────────────────────────────────────────

static std::recursive_mutex criticalMutex;
void host_critical_enter() { criticalMutex.lock(); }
void host_critical_exit() { criticalMutex.unlock(); }

// ── queues ───────────────────────────────────────────────────────────────────

struct HostQueue
{
    size_t capacity = 0;
    size_t itemSize = 0;
    std::deque<std::vector<uint8_t>> items;
    std::mutex mutex;
    std::condition_variable cv;
};

static std::map<QueueHandle_t, std::shared_ptr<HostQueue>> queues;
static std::mutex queuesMutex;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize)
{
    auto q = std::make_shared<HostQueue>();
    q->capacity = length;
    q->itemSize = itemSize;
    std::lock_guard<std::mutex> lock(queuesMutex);
    queues[(QueueHandle_t)q.get()] = q;
    return (QueueHandle_t)q.get();
}

void vQueueDelete(QueueHandle_t queue)
{
    std::lock_guard<std::mutex> lock(queuesMutex);
    queues.erase(queue);
}

BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t /*ticks*/)
{
    HostQueue* q = (HostQueue*)queue;
    if (!q || !item) return pdFALSE;
    std::lock_guard<std::mutex> lock(q->mutex);
    if (q->items.size() >= q->capacity) return pdFALSE;
    const uint8_t* bytes = (const uint8_t*)item;
    q->items.emplace_back(bytes, bytes + q->itemSize);
    q->cv.notify_all();
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void* out, TickType_t ticks)
{
    HostQueue* q = (HostQueue*)queue;
    if (!q || !out) return pdFALSE;
    std::unique_lock<std::mutex> lock(q->mutex);
    if (q->items.empty())
    {
        if (ticks == portMAX_DELAY)
            q->cv.wait(lock, [&] { return !q->items.empty(); });
        else if (!q->cv.wait_for(lock, std::chrono::milliseconds(ticks),
                                 [&] { return !q->items.empty(); }))
            return pdFALSE;
    }
    memcpy(out, q->items.front().data(), q->itemSize);
    q->items.pop_front();
    return pdTRUE;
}

// ── semaphores ───────────────────────────────────────────────────────────────

SemaphoreHandle_t xSemaphoreCreateMutex() { return (SemaphoreHandle_t)new std::timed_mutex(); }

void xSemaphoreGive(SemaphoreHandle_t sem) { ((std::timed_mutex*)sem)->unlock(); }

void vSemaphoreDelete(SemaphoreHandle_t sem) { delete (std::timed_mutex*)sem; }

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    std::timed_mutex* m = (std::timed_mutex*)sem;
    if (ticks == portMAX_DELAY)
    {
        m->lock();
        return pdTRUE;
    }
    return m->try_lock_for(std::chrono::milliseconds(ticks)) ? pdTRUE : pdFALSE;
}

// ── hang injection ───────────────────────────────────────────────────────────

std::string host_hang_task_name;
void host_set_hang(const char* taskName) { host_hang_task_name = taskName ? taskName : ""; }
