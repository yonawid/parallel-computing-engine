#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <utility>

namespace pce {

// Thread-safe FIFO queue of tasks.
//
// Invariants (all guarded by mutex_):
//  - tasks are only pushed while !closed_
//  - after close(), queued tasks remain poppable until drained
//  - tasks are never executed by the queue; callers run them without any lock held
class TaskQueue {
public:
    TaskQueue() = default;
    ~TaskQueue() = default;

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;
    TaskQueue(TaskQueue&&) = delete;
    TaskQueue& operator=(TaskQueue&&) = delete;

    // Returns false (and drops nothing from the queue) if the queue is closed
    // or the task is empty.
    bool push(std::function<void()> task) {
        if (!task) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return false;
            }
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
        return true;
    }

    // Blocks until a task is available or the queue is closed and empty.
    // Returns false only when closed and fully drained.
    bool wait_and_pop(std::function<void()>& task) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return closed_ || !tasks_.empty(); });
        if (tasks_.empty()) {
            return false;  // closed_ must be true here
        }
        task = std::move(tasks_.front());
        tasks_.pop();
        return true;
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

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> tasks_;
    bool closed_ = false;
};

}  // namespace pce
