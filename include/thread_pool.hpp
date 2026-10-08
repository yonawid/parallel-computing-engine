#pragma once

#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "task_queue.hpp"

namespace pce {

// Fixed-size pool of worker threads fed by a TaskQueue.
//
// Lifecycle: workers start in the constructor and exit once the queue is closed
// and drained. shutdown() closes the queue (rejecting further submissions),
// lets workers finish every already-accepted task, then joins them.
//
// Threading contract:
//  - submit() may be called concurrently from any number of threads.
//  - shutdown() is idempotent and may be called concurrently from external
//    threads; every caller returns only after all workers have been joined.
//  - shutdown() (and therefore destruction) must NOT be called from a task
//    running on this pool: a worker cannot join itself. Doing so throws
//    std::logic_error rather than deadlocking.
class ThreadPool {
public:
    // Throws std::invalid_argument if thread_count == 0. If a worker thread
    // cannot be created, already-started workers are stopped and joined before
    // the exception propagates.
    explicit ThreadPool(std::size_t thread_count);

    // Calls shutdown(); never throws.
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Schedules function(args...) and returns a future for its result.
    // The callable and arguments are decay-copied (or moved) into the task, so
    // no references to caller state are retained; use std::ref explicitly to
    // share. Exceptions thrown by the task are delivered through the future.
    // Throws std::runtime_error if the pool is shutting down or shut down.
    template <typename F, typename... Args>
    auto submit(F&& function, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using Result = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        auto task = std::make_shared<std::packaged_task<Result()>>(
            [fn = std::decay_t<F>(std::forward<F>(function)),
             tup = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)]() mutable
            -> Result { return std::apply(std::move(fn), std::move(tup)); });

        std::future<Result> future = task->get_future();
        if (!queue_.push([task = std::move(task)] { (*task)(); })) {
            throw std::runtime_error("ThreadPool::submit: pool has been shut down");
        }
        return future;
    }

    // Stops accepting tasks, runs all accepted tasks to completion, joins workers.
    void shutdown();

    std::size_t size() const noexcept { return thread_count_; }

private:
    void worker_loop();

    TaskQueue queue_;
    std::vector<std::thread> workers_;
    std::vector<std::thread::id> worker_ids_;  // immutable after construction
    std::size_t thread_count_;
    std::once_flag shutdown_flag_;
};

}  // namespace pce
