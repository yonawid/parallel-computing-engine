#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

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
        futures.push_back(pool.submit(sum_of_squares, i * 100000));
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

    pool.shutdown();

    std::cout << "--- summary ---\n"
              << "compute tasks completed: " << futures.size() << '\n'
              << "sum of results: " << grand_total << '\n'
              << "void task completed: yes\n"
              << "exception propagated: " << (caught ? "yes" : "no") << '\n';
    return caught ? 0 : 1;
}
