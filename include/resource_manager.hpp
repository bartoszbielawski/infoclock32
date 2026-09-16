#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <atomic>

#include <logger.hpp>

template<class R> class ResourceManager;

// A queued display request. `abandoned` lets a requester that timed out mark
// its entry so the manager skips it — otherwise the manager would later grant
// the display to a request nobody is waiting on, and wait forever for the
// release (permanent display lockup).
struct DisplayRequest
{
    TaskHandle_t task;
    std::atomic<bool> abandoned;
};

// RAII guard returned by ResourceManager::acquire().
// Releases the resource automatically on destruction.
template<class R>
class ResourceGuard
{
public:
    ~ResourceGuard() { if (acquired_) rmd_.release_access(); }

    explicit operator bool() const { return acquired_; }
    operator R&()   { return  resource_; }
    R* operator->() { return &resource_; }
    R& operator*()  { return  resource_; }

    ResourceGuard(const ResourceGuard&)            = delete;
    ResourceGuard& operator=(const ResourceGuard&) = delete;
    ResourceGuard(ResourceGuard&& o)
        : rmd_(o.rmd_), resource_(o.resource_), acquired_(o.acquired_)
        { o.acquired_ = false; }

private:
    friend class ResourceManager<R>;
    explicit ResourceGuard(ResourceManager<R>& rmd, TickType_t timeout)
        : rmd_(rmd), resource_(rmd.getResourceRef()), acquired_(rmd.make_access_request(timeout)) {}

    ResourceManager<R>& rmd_;
    R& resource_;
    bool acquired_;
};

template<class R>
class ResourceManager
{
public:
    ResourceManager()
        : resource(nullptr), current_task(nullptr),
          request_queue(nullptr), manager_task(nullptr),
          pre_release_hook(nullptr), drop_count_(0) {}

    static ResourceManager& getInstance()
    {
        static ResourceManager instance;
        return instance;
    }

    ~ResourceManager()
    {
        if (request_queue)
            vQueueDelete(request_queue);
    }

    void initialize(R* res, uint16_t queue_length = 3)
    {
        resource = res;
        request_queue = xQueueCreate(queue_length, sizeof(DisplayRequest));
        xTaskCreate(manager_task_function, "ResourceManager", 2048, this, 1, &manager_task);
    }

    static void manager_task_function(void* parameter)
    {
        ResourceManager* mgr = static_cast<ResourceManager*>(parameter);
        DisplayRequest request;
        while (true)
        {
            if (xQueueReceive(mgr->request_queue, &request, 1000 / portTICK_PERIOD_MS) == pdTRUE)
            {
                if (request.abandoned)
                    continue;   // requester timed out before we got here

                mgr->current_task = request.task;
                xTaskNotifyGive(request.task);
                // Bounded release wait: normally the holder releases via its
                // ResourceGuard. If it never does (crashed mid-hold, or a
                // razor-thin abandoned-request race), force the handover so
                // one stuck task can't starve the whole display. Must exceed
                // the longest legitimate hold (scrolls + Life burst).
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000)) == 0)
                    logPrintf("RES", "%s: display holder did not release — forcing handover",
                              pcTaskGetName(mgr->current_task));
                mgr->current_task = nullptr;
            }
        }
    }

    // Acquire the resource, blocking up to `timeout` ticks.
    // Returns a guard that releases automatically on destruction.
    ResourceGuard<R> acquire(TickType_t timeout = portMAX_DELAY)
    {
        return ResourceGuard<R>(*this, timeout);
    }

    bool make_access_request(TickType_t timeout = portMAX_DELAY)
    {
        TaskHandle_t requester = xTaskGetCurrentTaskHandle();
        // Drop any stale grant left over from an abandoned request — it would
        // otherwise be mistaken for a fresh one.
        ulTaskNotifyTake(pdTRUE, 0);

        DisplayRequest request;
        request.task      = requester;
        request.abandoned = false;
        if (xQueueSend(request_queue, &request, 0) != pdTRUE)
        {
            drop_count_++;
            logPrintf("RES", "%s: queue full", pcTaskGetName(requester));
            return false;
        }
        if (ulTaskNotifyTake(pdTRUE, timeout) == 0)
        {
            drop_count_++;
            logPrintf("RES", "%s: timed out waiting for display", pcTaskGetName(requester));
            // Retract the queued request and drain any grant that may already
            // be in flight, handing it back, so the manager never waits for a
            // release that will never come.
            request.abandoned = true;
            while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) != 0)
                xTaskNotifyGive(manager_task);
            return false;
        }
        return true;
    }

    void setPreReleaseHook(void (*hook)(R&)) { pre_release_hook = hook; }

    void release_access()
    {
        if (xTaskGetCurrentTaskHandle() == current_task)
        {
            if (pre_release_hook)
                pre_release_hook(*resource);
            xTaskNotifyGive(manager_task);
        }
    }

    R* getResource()    { return resource; }
    R& getResourceRef() { return *resource; }

    uint32_t getDropCount() const { return drop_count_; }

private:
    R*            resource;
    TaskHandle_t  current_task;
    QueueHandle_t request_queue;
    TaskHandle_t  manager_task;
    void (*pre_release_hook)(R&);
    uint32_t      drop_count_;
};

#endif // RESOURCE_MANAGER_HPP
