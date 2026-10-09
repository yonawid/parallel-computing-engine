#include "thread_pool.hpp"

#include <algorithm>

namespace pce {

thread_local const ThreadPool* tls_current_pool = nullptr;

ThreadPool::ThreadPool(std::size_t thread_count, SchedulingPolicy policy) : policy_(policy), thread_count_(thread_count) {
    if (thread_count == 0) {
        throw std::invalid_argument("ThreadPool: thread_count must be at least 1");
    }
    
    if (policy_ == SchedulingPolicy::WorkStealing) {
        worker_deques_.reserve(thread_count_);
        for (std::size_t i = 0; i < thread_count_; ++i) {
            worker_deques_.push_back(std::make_unique<WorkerDeque>());
        }
    }

    workers_.reserve(thread_count);

    try {
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this, i] { worker_loop(i); });
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
        if (policy_ == SchedulingPolicy::WorkStealing) {
            ws_closed_.store(true, std::memory_order_release);
            ws_event_counter_.fetch_add(1, std::memory_order_release);
            ws_event_counter_.notify_all();
        } else {
            queue_.close();
        }
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    });
}

void ThreadPool::worker_loop(std::size_t worker_index) {
    const ThreadPool* const prev_pool = tls_current_pool;
    tls_current_pool = this;

    // Fast pseudo-random generator for stealing
    uint32_t seed = static_cast<uint32_t>(worker_index + 1) * 0x85ebca6b;
    auto next_rand = [&]() -> uint32_t {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };

    while (true) {
        auto overhead_start = std::chrono::steady_clock::now();
        std::function<void()> task;

        if (policy_ == SchedulingPolicy::WorkStealing) {
            // 1. Try local pop
            if (!worker_deques_[worker_index]->pop_bottom(task)) {
                // 2. Try steal
                bool stole = false;
                if (thread_count_ > 1) {
                    for (std::size_t i = 0; i < thread_count_; ++i) {
                        std::size_t victim = next_rand() % thread_count_;
                        if (victim == worker_index) continue;
                        
                        if (worker_deques_[victim]->steal_top(task)) {
                            stole = true;
                            stats_.record_steal_attempt(true);
                            break;
                        } else {
                            stats_.record_steal_attempt(false);
                        }
                    }
                }
                
                if (!stole) {
                    // Calculate overhead before sleeping
                    auto overhead_end = std::chrono::steady_clock::now();
                    stats_.record_overhead(overhead_end - overhead_start);
                    
                    // 3. Sleep on global CV using lock-free event counter
                    uint64_t cmp = ws_event_counter_.load(std::memory_order_acquire);
                    
                    bool has_work = false;
                    for (auto& d : worker_deques_) {
                        if (!d->empty()) { has_work = true; break; }
                    }
                    if (has_work) continue;
                    
                    if (ws_closed_.load(std::memory_order_acquire)) {
                        break; // Shutdown and empty
                    }

                    ws_event_counter_.wait(cmp, std::memory_order_acquire);
                    
                    // Start next iteration (we woke up, maybe work is available now)
                    continue; 
                }
            }
            
            if (task) {
                auto overhead_end = std::chrono::steady_clock::now();
                stats_.record_overhead(overhead_end - overhead_start);
            }

        } else {
            // Priority or FIFO
            std::chrono::steady_clock::duration idle_duration(0);
            if (!queue_.wait_and_pop(task, &idle_duration)) {
                auto overhead_end = std::chrono::steady_clock::now();
                stats_.record_overhead((overhead_end - overhead_start) - idle_duration);
                break;
            }
            auto overhead_end = std::chrono::steady_clock::now();
            stats_.record_overhead((overhead_end - overhead_start) - idle_duration);
        }

        if (task) {
            stats_.on_started();
            task();  // no lock held; exceptions are captured by the packaged_task
            task = nullptr;  // release captured state promptly
        }
    }

    tls_current_pool = prev_pool;
}

}  // namespace pce
