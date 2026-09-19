#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <atomic>
#include <cstdint>

#include <logger.hpp>

template<class R> class ResourceManager;

// A queued display request. `gen` identifies the acquire attempt; the manager
// matches it against the pending-request registry so a request whose owner
// already timed out can be recognized (and skipped) when it is finally popped.
struct DisplayRequest
{
    TaskHandle_t task;
    uint32_t     gen;
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
    explicit ResourceGuard(ResourceManager<R>& rmd, TickType_t timeout, bool priority)
        : rmd_(rmd), resource_(rmd.getResourceRef()), acquired_(rmd.make_access_request(timeout, priority)) {}

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
          request_queue(nullptr), fast_queue(nullptr), manager_task(nullptr),
          pre_release_hook(nullptr), drop_count_(0), gen_counter_(0) {}

    static ResourceManager& getInstance()
    {
        static ResourceManager instance;
        return instance;
    }

    ~ResourceManager()
    {
        if (request_queue)
            vQueueDelete(request_queue);
        if (fast_queue)
            vQueueDelete(fast_queue);
    }

    // `fast_queue` is the priority lane: requests queued there (clock, user
    // pushes) are always served before requests waiting in `request_queue`.
    void initialize(R* res, uint16_t queue_length = 3, uint16_t fast_queue_length = 2)
    {
        resource = res;
        taskENTER_CRITICAL(&pend_mux_);
        for (auto& p : pending_)
            p.task = nullptr;
        taskEXIT_CRITICAL(&pend_mux_);
        request_queue = xQueueCreate(queue_length, sizeof(DisplayRequest));
        fast_queue    = xQueueCreate(fast_queue_length, sizeof(DisplayRequest));
        xTaskCreate(manager_task_function, "ResourceManager", 2048, this, 1, &manager_task);
    }

    static void manager_task_function(void* parameter)
    {
        ResourceManager* mgr = static_cast<ResourceManager*>(parameter);
        DisplayRequest request;
        while (true)
        {
            // Serve the priority lane first, then fall back to the normal
            // queue. A priority request arriving during a hold is granted
            // right after the release, ahead of queued normal requests.
            if (xQueueReceive(mgr->fast_queue, &request, 0) != pdTRUE &&
                xQueueReceive(mgr->request_queue, &request, 1000 / portTICK_PERIOD_MS) != pdTRUE)
                continue;

            // Skip requests whose owner gave up while they sat queued —
            // granting them would notify a task that is no longer waiting
            // and then stall the display for the whole release timeout.
            // Claiming also marks a live request GRANTED atomically.
            if (!mgr->claimPending(request.task, request.gen))
                continue;

            mgr->current_task = request.task;
            mgr->last_progress_ms_ = (uint32_t)millis();
            xTaskNotifyGive(request.task);
            // Bounded release wait: normally the holder releases via its
            // ResourceGuard, and a requester that times out right after the
            // grant ends the wait via the give-up channel. The wait is also
            // vetoed if the holder stops showing progress (renewHold) for
            // kForceHandoverMs — that means a crashed or wedged holder, and
            // the handover is forced so one stuck task can't starve the
            // display. Legitimate long holds (slow scrolls, Life bursts)
            // renew continuously and are never stolen mid-frame.
            bool released = false;
            while (!released)
            {
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) != 0)
                    released = true;
                else if ((uint32_t)millis() - mgr->last_progress_ms_.load() > kForceHandoverMs)
                {
                    mgr->force_handovers_++;
                    mgr->last_force_handover_ms_ = (uint32_t)millis();
                    logPrintf("RES", "%s: display holder did not release — forcing handover",
                              pcTaskGetName(mgr->current_task.load()));
                    break;
                }
            }
            mgr->current_task = nullptr;
        }
    }

    // Acquire the resource, blocking up to `timeout` ticks.
    // `priority` queues the request on the fast lane (clock, user pushes).
    // Returns a guard that releases automatically on destruction.
    ResourceGuard<R> acquire(TickType_t timeout = portMAX_DELAY, bool priority = false)
    {
        return ResourceGuard<R>(*this, timeout, priority);
    }

    // True when a priority-lane request (clock, user push) is waiting. Long
    // holds (scrolls, Life bursts) poll this periodically and cut themselves
    // short, so a fast-lane requester is granted at the next release instead
    // of waiting out the rest of the hold. Short holds are not required to
    // check (see graphic_utils.cpp for the hold-time policy).
    bool yieldRequested() const
    {
        return fast_queue && uxQueueMessagesWaiting(fast_queue) > 0;
    }

    // A caller blocks here until granted or timed out, so it can never have
    // more than one pending request queued at a time — stale duplicates are
    // impossible by construction.
    bool make_access_request(TickType_t timeout = portMAX_DELAY, bool priority = false)
    {
        TaskHandle_t requester = xTaskGetCurrentTaskHandle();
        // Drop any stale grant left over from an abandoned request — it would
        // otherwise be mistaken for a fresh one.
        ulTaskNotifyTake(pdTRUE, 0);

        uint32_t gen;
        if (!registerPending(requester, gen))
        {
            drop_count_++;
            logPrintf("RES", "%s: pending table full", pcTaskGetName(requester));
            return false;
        }

        DisplayRequest request;
        request.task = requester;
        request.gen  = gen;
        if (xQueueSend(priority ? fast_queue : request_queue, &request, 0) != pdTRUE)
        {
            drop_count_++;
            removePending(requester, gen);
            logPrintf("RES", "%s: queue full", pcTaskGetName(requester));
            return false;
        }
        if (ulTaskNotifyTake(pdTRUE, timeout) == 0)
        {
            drop_count_++;
            logPrintf("RES", "%s: timed out waiting for display", pcTaskGetName(requester));
            // Mark the attempt dead so the manager skips it if it is still
            // queued. If the manager had already claimed the grant, end its
            // release wait from here: swallow a late-delivered grant (it
            // must not leak into the next acquire) and signal the give-up —
            // either one ends the manager's wait.
            PendState previous = setPendingState(requester, gen, PendState::DEAD);
            if (previous == PendState::GRANTED)
            {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kGiveUpCatchMs));
                xTaskNotifyGive(manager_task);
            }
            return false;
        }
        // Granted: retire the entry; the display is ours until release_access().
        removePending(requester, gen);
        return true;
    }

    void setPreReleaseHook(void (*hook)(R&)) { pre_release_hook = hook; }

    // Called by the current holder while it draws, so the manager's
    // force-handover timer tracks real progress. Long scrolls and Life
    // bursts call this every frame; without it a legitimate hold longer
    // than kForceHandoverMs would get its display stolen mid-frame.
    void renewHold() { last_progress_ms_ = (uint32_t)millis(); }

    void release_access()
    {
        if (xTaskGetCurrentTaskHandle() == current_task.load())
        {
            if (pre_release_hook)
                pre_release_hook(*resource);
            xTaskNotifyGive(manager_task);
        }
    }

    R* getResource()    { return resource; }
    R& getResourceRef() { return *resource; }

    uint32_t getDropCount() const { return drop_count_.load(); }

    // ── diagnostics (safe to call from any task) ────────────────────────────
    // Name of the task currently holding the display, or nullptr when idle.
    const char* getCurrentHolder() const
    {
        TaskHandle_t t = current_task.load();
        return t ? pcTaskGetName(t) : nullptr;
    }

    // Requests waiting in the normal lane (fastLane == false) or priority lane.
    UBaseType_t getQueueDepth(bool fastLane) const
    {
        QueueHandle_t q = fastLane ? fast_queue : request_queue;
        return q ? uxQueueMessagesWaiting(q) : 0;
    }

    // Force handovers: total count and millis() timestamp of the last one
    // (0 = never happened). The age has to be computed by the caller.
    uint32_t getForceHandoverCount() const   { return force_handovers_.load(); }
    uint32_t getLastForceHandoverMs() const  { return last_force_handover_ms_.load(); }

private:
    // Lifecycle of one acquire attempt, tracked in a small registry so the
    // manager can tell a live requester from one that already gave up:
    //   WAITING → GRANTED (manager claims the request and commits the grant)
    //   WAITING → DEAD    (requester timed out before the manager claimed it)
    //   GRANTED → DEAD    (requester timed out after the grant — it then ends
    //                      the manager's release wait via the give-up channel)
    //   GRANTED → removed (requester received the grant and owns the display)
    // Entries are keyed by (task, gen): a task re-acquiring while one of its
    // earlier dead requests is still queued must not resurrect that request.
    // The registry is only read by the manager BEFORE granting (claimPending);
    // after a claim, the release wait is bounded by notifications alone
    // (release / give-up / force handover), so later registry changes —
    // including the owner replacing its entry — cannot stall it.
    // GONE is never stored — helpers report it for missing entries.
    enum class PendState : uint8_t { WAITING, GRANTED, DEAD, GONE };
    struct PendingRequest
    {
        TaskHandle_t task;
        uint32_t     gen;
        PendState    state;
    };

    static constexpr int      kMaxPending      = 8;
    static constexpr uint32_t kGiveUpCatchMs   = 100;    // late-grant swallow window
    static constexpr uint32_t kForceHandoverMs = 30000;  // max hold without progress

    PendingRequest* findByTaskLocked(TaskHandle_t task)
    {
        for (auto& p : pending_)
            if (p.task == task) return &p;
        return nullptr;
    }

    PendingRequest* findPendingLocked(TaskHandle_t task, uint32_t gen)
    {
        PendingRequest* p = findByTaskLocked(task);
        return (p && p->gen == gen) ? p : nullptr;
    }

    bool registerPending(TaskHandle_t task, uint32_t& genOut)
    {
        bool ok = false;
        taskENTER_CRITICAL(&pend_mux_);
        PendingRequest* p = findByTaskLocked(task);   // replace any leftover
        if (!p)
            for (auto& slot : pending_)
                if (!slot.task) { p = &slot; break; }
        if (p)
        {
            p->task  = task;
            p->gen   = ++gen_counter_;
            p->state = PendState::WAITING;
            genOut   = p->gen;
            ok = true;
        }
        taskEXIT_CRITICAL(&pend_mux_);
        return ok;
    }

    // Atomically claim a live request (WAITING → GRANTED) or retire a dead
    // one. Returns false when the request must be skipped.
    bool claimPending(TaskHandle_t task, uint32_t gen)
    {
        bool ok = false;
        taskENTER_CRITICAL(&pend_mux_);
        PendingRequest* p = findPendingLocked(task, gen);
        if (p)
        {
            if (p->state == PendState::WAITING)
            {
                p->state = PendState::GRANTED;
                ok = true;
            }
            else
            {
                p->task = nullptr;   // retire the dead entry
            }
        }
        taskEXIT_CRITICAL(&pend_mux_);
        return ok;
    }

    // Sets a new state and returns the previous one (GONE if no entry).
    PendState setPendingState(TaskHandle_t task, uint32_t gen, PendState st)
    {
        PendState old = PendState::GONE;
        taskENTER_CRITICAL(&pend_mux_);
        if (PendingRequest* p = findPendingLocked(task, gen))
        {
            old = p->state;
            p->state = st;
        }
        taskEXIT_CRITICAL(&pend_mux_);
        return old;
    }

    void removePending(TaskHandle_t task, uint32_t gen)
    {
        taskENTER_CRITICAL(&pend_mux_);
        if (PendingRequest* p = findPendingLocked(task, gen)) p->task = nullptr;
        taskEXIT_CRITICAL(&pend_mux_);
    }

    R*                        resource;
    std::atomic<TaskHandle_t> current_task;
    QueueHandle_t             request_queue;
    QueueHandle_t             fast_queue;
    TaskHandle_t              manager_task;
    void                      (*pre_release_hook)(R&);
    std::atomic<uint32_t>     drop_count_;
    std::atomic<uint32_t>     last_progress_ms_{0};   // last renewHold()/grant
    std::atomic<uint32_t>     force_handovers_{0};
    std::atomic<uint32_t>     last_force_handover_ms_{0};
    PendingRequest            pending_[kMaxPending] = {};
    portMUX_TYPE              pend_mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint32_t                  gen_counter_;   // guarded by pend_mux_
};

#endif // RESOURCE_MANAGER_HPP
