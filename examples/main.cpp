#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "task_priority.hpp"
#include "thread_pool.hpp"

namespace {

std::uint64_t sum_of_squares(std::uint64_t n) {
    std::uint64_t total = 0;
    for (std::uint64_t i = 1; i <= n; ++i) {
        total += i * i;
    }
    return total;
}

}  // namespace

int main() {
    pce::ThreadPool pool(4);
    std::cout << "ThreadPool started with " << pool.size() << " workers\n";

    std::vector<std::future<std::uint64_t>> futures;
    for (std::uint64_t i = 1; i <= 10; ++i) {
        // High priority tasks should jump to the front of the queue
        auto priority = (i % 2 == 0) ? pce::TaskPriority::High : pce::TaskPriority::Low;
        futures.push_back(pool.submit_with_priority(priority, sum_of_squares, i * 100000));
    }

    auto void_task = pool.submit([] { std::cout << "void task executed\n"; });
    auto failing = pool.submit([]() -> int { throw std::runtime_error("intentional failure"); });

    std::uint64_t grand_total = 0;
    for (std::size_t i = 0; i < futures.size(); ++i) {
        const auto value = futures[i].get();
        std::cout << "task " << i << " -> " << value << '\n';
        grand_total += value;
    }
    void_task.get();

    bool caught = false;
    try {
        failing.get();
    } catch (const std::exception& e) {
        caught = true;
        std::cout << "caught exception from future: " << e.what() << '\n';
    }

    auto stats = pool.get_stats();
    std::cout << "Before shutdown, active workers: " << pool.active_worker_count() << "\n";

    pool.shutdown();
    
    stats = pool.get_stats();

    std::cout << "--- summary ---\n"
              << "compute tasks completed: " << futures.size() << '\n'
              << "sum of results: " << grand_total << '\n'
              << "void task completed: yes\n"
              << "exception propagated: " << (caught ? "yes" : "no") << '\n'
              << "stats: accepted=" << stats.tasks_submitted 
              << ", completed=" << stats.tasks_completed 
              << ", failed=" << stats.tasks_failed 
              << ", max_active=" << stats.peak_active_workers << '\n';
              
    return caught ? 0 : 1;
}
