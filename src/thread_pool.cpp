#include "thread_pool.hpp"

#include <algorithm>

namespace pce {

thread_local const ThreadPool* tls_current_pool = nullptr;

ThreadPool::ThreadPool(std::size_t thread_count) : thread_count_(thread_count) {
    if (thread_count == 0) {
        throw std::invalid_argument("ThreadPool: thread_count must be at least 1");
    }
    workers_.reserve(thread_count);

    try {
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        // Joinable std::thread objects must never be destroyed.
        queue_.close();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }
}

ThreadPool::~ThreadPool() {
    try {
        shutdown();
    } catch (...) {
        // Destructor must not throw; shutdown() only throws for the misuse
        // case (called from a worker), which cannot be recovered here.
    }
}

void ThreadPool::shutdown() {
    if (tls_current_pool == this) {
        throw std::logic_error("ThreadPool::shutdown called from one of its own workers");
    }
    // call_once makes concurrent callers wait until the first one has joined.
    std::call_once(shutdown_flag_, [this] {
        queue_.close();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    });
}

void ThreadPool::worker_loop() {
    const ThreadPool* const prev_pool = tls_current_pool;
    tls_current_pool = this;

    std::function<void()> task;
    while (queue_.wait_and_pop(task)) {
        task();  // no lock held; exceptions are captured by the packaged_task
        task = nullptr;  // release captured state promptly
    }

    tls_current_pool = prev_pool;
}

}  // namespace pce
