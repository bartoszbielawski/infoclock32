#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <logger.hpp>

template<class R> class ResourceManager;

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
        request_queue = xQueueCreate(queue_length, sizeof(TaskHandle_t));
        xTaskCreate(manager_task_function, "ResourceManager", 2048, this, 1, &manager_task);
    }

    static void manager_task_function(void* parameter)
    {
        ResourceManager* mgr = static_cast<ResourceManager*>(parameter);
        TaskHandle_t requesting_task = nullptr;
        while (true)
        {
            if (xQueueReceive(mgr->request_queue, &requesting_task, 1000 / portTICK_PERIOD_MS) == pdTRUE)
            {
                mgr->current_task = requesting_task;
                xTaskNotifyGive(requesting_task);
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
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
        if (xQueueSend(request_queue, &requester, 0) != pdTRUE)
        {
            drop_count_++;
            logPrintf("RES", "%s: queue full", pcTaskGetName(requester));
            return false;
        }
        if (ulTaskNotifyTake(pdTRUE, timeout) == 0)
        {
            drop_count_++;
            logPrintf("RES", "%s: timed out waiting for display", pcTaskGetName(requester));
            // Consume any in-flight grant to prevent manager deadlock.
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)))
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
