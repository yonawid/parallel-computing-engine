#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>

#include "task_priority.hpp"

namespace pce {

// Thread-safe priority queue of tasks: one FIFO lane per TaskPriority, all
// guarded by a single mutex and condition variable.
//
// Invariants (all guarded by mutex_):
//  - tasks are only pushed while !closed_
//  - total_ == sum of the sizes of all lanes
//  - wait_and_pop() returns the oldest task of the highest non-empty lane
//    (strict priority, FIFO within a priority)
//  - after close(), queued tasks remain poppable until every lane is drained
//  - tasks are never executed by the queue; callers run them without any lock held
//
// Starvation: scheduling is strict priority with no aging. Under a continuous
// stream of higher-priority tasks, lower-priority tasks may wait indefinitely.
class TaskQueue {
public:
    TaskQueue() = default;
    ~TaskQueue() = default;

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;
    TaskQueue(TaskQueue&&) = delete;
    TaskQueue& operator=(TaskQueue&&) = delete;

    // Phase 1 API: pushes at Normal priority.
    bool push(std::function<void()> task) {
        return push(TaskPriority::Normal, std::move(task));
    }

    bool push(TaskPriority priority, std::function<void()> task) {
        return push(priority, std::move(task), []() noexcept {});
    }

    // Returns false (queue unchanged) if the queue is closed, the task is
    // empty, or the priority is not a valid enumerator.
    //
    // on_accepted is invoked exactly once, while the queue mutex is held, only
    // if the task was accepted. It is the linearization point for "accepted"
    // bookkeeping: a worker cannot pop the task before on_accepted has run. It
    // must be fast, must not throw, and must not call back into this queue
    // (lock order is always queue mutex -> callback's own locks).
    template <typename OnAccepted>
    bool push(TaskPriority priority, std::function<void()> task, OnAccepted&& on_accepted) {
        static_assert(std::is_nothrow_invocable_v<OnAccepted&>,
                      "on_accepted must be noexcept: the task is already queued when it runs");
        const std::size_t lane = priority_index(priority);
        if (!task || lane >= kPriorityLevels) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return false;
            }
            lanes_[lane].push(std::move(task));
            ++total_;
            on_accepted();
        }
        cv_.notify_one();
        return true;
    }

    // Blocks until a task is available or the queue is closed and empty.
    // Returns false only when closed and fully drained (all priorities).
    bool wait_and_pop(std::function<void()>& task) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return closed_ || total_ != 0; });
        if (total_ == 0) {
            return false;  // closed_ must be true here
        }
        for (std::size_t lane = kPriorityLevels; lane-- > 0;) {
            auto& q = lanes_[lane];
            if (!q.empty()) {
                task = std::move(q.front());
                q.pop();
                --total_;
                return true;
            }
        }
        return false;  // unreachable: total_ != 0 implies a non-empty lane
    }

    // Idempotent. Wakes every waiting consumer.
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    // Total queued tasks across all priorities.
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_;
    }

    // Queued tasks at one priority.
    std::size_t size(TaskPriority priority) const {
        const std::size_t lane = priority_index(priority);
        if (lane >= kPriorityLevels) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return lanes_[lane].size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> lanes_[kPriorityLevels];
    std::size_t total_ = 0;
    bool closed_ = false;
};

}  // namespace pce
