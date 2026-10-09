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

int main() {
    try {
        test_fifo_ordering();
        test_work_stealing_execution();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
