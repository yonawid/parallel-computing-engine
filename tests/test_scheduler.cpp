#include <atomic>
#include <chrono>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <cassert>

#include "thread_pool.hpp"

using namespace pce;
using namespace std::chrono_literals;

void test_fifo_ordering() {
    std::cout << "[RUN] test_fifo_ordering\n";
    ThreadPool pool(1, SchedulingPolicy::FIFO);
    std::vector<int> execution_order;
    std::mutex m;

    for (int i = 0; i < 50; ++i) {
        pool.submit_with_priority(
            (i % 2 == 0) ? TaskPriority::High : TaskPriority::Low,
            [&execution_order, &m, i]() {
                if (i == 0) {
                    std::this_thread::sleep_for(100ms);
                }
                std::lock_guard<std::mutex> lock(m);
                execution_order.push_back(i);
            });
    }

    pool.shutdown();

    if (execution_order.size() != 50) throw std::runtime_error("Wrong size");
    for (int i = 0; i < 50; ++i) {
        if (execution_order[i] != i) throw std::runtime_error("Out of order execution in FIFO");
    }
    std::cout << "[PASS] test_fifo_ordering\n";
}

void test_work_stealing_execution() {
    std::cout << "[RUN] test_work_stealing_execution\n";
    ThreadPool pool(4, SchedulingPolicy::WorkStealing);
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 1000; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter++;
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    if (counter.load() != 1000) throw std::runtime_error("Work stealing missed tasks");
    std::cout << "[PASS] test_work_stealing_execution\n";
}

void test_work_stealing_lost_wakeup() {
    std::cout << "[RUN] test_work_stealing_lost_wakeup\n";
    ThreadPool pool(4, SchedulingPolicy::WorkStealing);
    std::atomic<int> counter{0};
    
    // Let workers go idle
    std::this_thread::sleep_for(100ms);

    // Submit a single task and wait, to ensure it doesn't get lost
    for (int i = 0; i < 100; ++i) {
        auto f = pool.submit([&counter]() { counter++; });
        f.get();
        std::this_thread::sleep_for(2ms);
    }

    if (counter.load() != 100) throw std::runtime_error("Lost wakeup detected");
    std::cout << "[PASS] test_work_stealing_lost_wakeup\n";
}

void test_work_stealing_uneven_load() {
    std::cout << "[RUN] test_work_stealing_uneven_load\n";
    ThreadPool pool(4, SchedulingPolicy::WorkStealing);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    std::mutex futures_mutex;

    // Concurrently submit from multiple threads to hammer the queues
    auto producer = [&pool, &counter, &futures, &futures_mutex](int n) {
        for (int i = 0; i < n; ++i) {
            auto f = pool.submit([&counter]() {
                std::this_thread::sleep_for(1ms); // Simulate small work
                counter++;
            });
            std::lock_guard<std::mutex> lock(futures_mutex);
            futures.push_back(std::move(f));
        }
    };

    std::thread p1(producer, 100);
    std::thread p2(producer, 100);
    p1.join();
    p2.join();

    for (auto& f : futures) {
        f.get();
    }

    if (counter.load() != 200) throw std::runtime_error("Uneven load failed");
    std::cout << "[PASS] test_work_stealing_uneven_load\n";
}

int main() {
    try {
        test_fifo_ordering();
        test_work_stealing_execution();
        test_work_stealing_lost_wakeup();
        test_work_stealing_uneven_load();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
