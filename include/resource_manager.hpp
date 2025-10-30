#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

// This class owns the handle to a resource and will wake up tasks that are
// waiting for the resource to be free. It runs a dedicated task to manage access to the resource.
// Only one task can access the resource at a time.
// Requests are held in a queue and processed in FIFO order.

template<class R>
class ResourceManager
{
public:
    ResourceManager()
    {
        resource = nullptr;
        current_task = nullptr;
        request_queue = nullptr;
        manager_task = nullptr;
    }   

    static ResourceManager& getInstance()
    {
        static ResourceManager instance;
        return instance;
    }

    ~ResourceManager()
    {
        if (request_queue)
        {
            vQueueDelete(request_queue);
        }
    }

    void initialize(R* resource, uint16_t queue_length = 3)
    {
        this->resource = resource;
        request_queue = xQueueCreate(queue_length, sizeof(TaskHandle_t));
        xTaskCreate(manager_task_function, "ResourceManager", 2048, this, 1, &manager_task);
    }

    static void manager_task_function(void *parameter)
    {
        ResourceManager *manager = static_cast<ResourceManager *>(parameter);

        //this will be populated by the task that wants to access the resource
        TaskHandle_t requesting_task = nullptr;

        //Serial.println("ResourceManager: Task started");
        while (true)
        {
            // Wait for a task to request access
            if (xQueueReceive(manager->request_queue, &requesting_task, 1000 / portTICK_PERIOD_MS) == pdTRUE)
            {
                //Serial.printf("ResourceManager: Task %s granted access\n", pcTaskGetName(requesting_task));
                // Grant access to the requesting task
                manager->current_task = requesting_task;
                xTaskNotifyGive(requesting_task);

                // Wait for the task to release the display
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                manager->current_task = nullptr;
                //Serial.printf("ResourceManager: Task %s released access\n", pcTaskGetName(requesting_task));
            }

            //Serial.printf("ResourceManager: %d tasks waiting\n", uxQueueMessagesWaiting(manager->request_queue));
        }
    }

    bool make_access_request()
    {
        TaskHandle_t requester = xTaskGetCurrentTaskHandle();
        //Serial.printf("ResourceManager: Task %s making access request\n", pcTaskGetName(requester));
        if (xQueueSend(request_queue, &requester, 0) != pdTRUE)
            return false;
        
        // wait for the display manager to grant access
        // the task will be suspended and resumed by the display manager
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        return true;
    }

    void release_access()
    {
        // Only the task that currently has access can release it
        if (xTaskGetCurrentTaskHandle() == current_task)
        {
            //Serial.printf("ResourceManager: Task %s releasing access\n", pcTaskGetName(current_task));
            // Notify the display manager that the task is done
            xTaskNotifyGive(manager_task);
        }
    }

    R* getResource()
    {
        return resource;
    }

    R& getResourceRef()
    {
        return *resource;
    }

private:
    R *resource;
    TaskHandle_t current_task;
    QueueHandle_t request_queue;
    TaskHandle_t manager_task;
};


#endif // RESOURCE_MANAGER_HPP
  