#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "task_queue.hpp"
#include "thread_pool.hpp"

namespace {

using namespace std::chrono_literals;
constexpr auto kTimeout = 20s;

int g_checks = 0;
int g_failures = 0;
std::mutex g_report_mutex;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        std::lock_guard<std::mutex> report_lock(g_report_mutex);                        \
        ++g_checks;                                                                     \
        if (!(cond)) {                                                                  \
            ++g_failures;                                                               \
            std::cerr << "  CHECK FAILED: " #cond " (" << __FILE__ << ':' << __LINE__    \
                      << ")\n";                                                         \
        }                                                                               \
    } while (false)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto va = (a);                                                            \
        const auto vb = (b);                                                            \
        std::lock_guard<std::mutex> report_lock(g_report_mutex);                        \
        ++g_checks;                                                                     \
        if (!(va == vb)) {                                                              \
            ++g_failures;                                                               \
            std::cerr << "  CHECK_EQ FAILED: " #a " == " #b " (got " << va << " vs "    \
                      << vb << ") at " << __FILE__ << ':' << __LINE__ << '\n';          \
        }                                                                               \
    } while (false)

// Waits for a future with a timeout; a timeout is reported as a failure.
template <typename T>
bool ready(std::future<T>& f) {
    const bool ok = f.wait_for(kTimeout) == std::future_status::ready;
    CHECK(ok);
    return ok;
}

// Runs a test body on a watchdog: if it hangs, report and abort the process.
struct Test {
    const char* name;
    void (*fn)();
};

void test_single_worker() {
    pce::ThreadPool pool(1);
    CHECK_EQ(pool.size(), std::size_t{1});
    auto f = pool.submit([] { return 42; });
    if (ready(f)) CHECK_EQ(f.get(), 42);
}

void test_multiple_workers() {
    pce::ThreadPool pool(8);
    CHECK_EQ(pool.size(), std::size_t{8});
    // All 8 tasks must run concurrently: each waits for the others at a latch-like barrier.
    std::atomic<int> arrived{0};
    std::vector<std::future<bool>> fs;
    for (int i = 0; i < 8; ++i) {
        fs.push_back(pool.submit([&arrived] {
            arrived.fetch_add(1);
            const auto deadline = std::chrono::steady_clock::now() + kTimeout;
            while (arrived.load() < 8) {
                if (std::chrono::steady_clock::now() > deadline) return false;
                std::this_thread::yield();
            }
            return true;
        }));
    }
    for (auto& f : fs) {
        if (ready(f)) CHECK(f.get());
    }
}

void test_zero_threads_rejected() {
    bool thrown = false;
    try {
        pce::ThreadPool pool(0);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    CHECK(thrown);
}

void test_hundred_tasks() {
    pce::ThreadPool pool(4);
    std::vector<std::future<int>> fs;
    for (int i = 0; i < 100; ++i) fs.push_back(pool.submit([](int x) { return x * x; }, i));
    for (int i = 0; i < 100; ++i) {
        if (ready(fs[i])) CHECK_EQ(fs[i].get(), i * i);
    }
}

void test_concurrent_producers() {
    constexpr int kProducers = 8;
    constexpr int kPerProducer = 500;
    pce::ThreadPool pool(4);
    std::atomic<int> executed{0};
    std::vector<std::vector<std::future<int>>> results(kProducers);
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                results[p].push_back(pool.submit([&executed, p, i] {
                    executed.fetch_add(1);
                    return p * 1000000 + i;
                }));
            }
        });
    }
    for (auto& t : producers) t.join();
    for (int p = 0; p < kProducers; ++p) {
        for (int i = 0; i < kPerProducer; ++i) {
            if (ready(results[p][i])) CHECK_EQ(results[p][i].get(), p * 1000000 + i);
        }
    }
    CHECK_EQ(executed.load(), kProducers * kPerProducer);
}

int add(int a, int b) { return a + b; }

void test_non_void_results() {
    pce::ThreadPool pool(2);
    auto a = pool.submit(add, 2, 3);
    auto b = pool.submit([](const std::string& s) { return s + "!"; }, std::string("hi"));
    auto c = pool.submit([] { return 3.5; });
    if (ready(a)) CHECK_EQ(a.get(), 5);
    if (ready(b)) CHECK_EQ(b.get(), std::string("hi!"));
    if (ready(c)) CHECK_EQ(c.get(), 3.5);
}

void test_void_tasks() {
    pce::ThreadPool pool(3);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> fs;
    for (int i = 0; i < 50; ++i) fs.push_back(pool.submit([&counter] { counter.fetch_add(1); }));
    for (auto& f : fs) {
        if (ready(f)) f.get();
    }
    CHECK_EQ(counter.load(), 50);
}

void test_exception_propagation() {
    pce::ThreadPool pool(2);
    auto f = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    auto g = pool.submit([] { throw std::logic_error("void boom"); });
    bool got_runtime = false, got_logic = false;
    if (ready(f)) {
        try { f.get(); } catch (const std::runtime_error& e) { got_runtime = std::string(e.what()) == "boom"; }
    }
    if (ready(g)) {
        try { g.get(); } catch (const std::logic_error&) { got_logic = true; }
    }
    CHECK(got_runtime);
    CHECK(got_logic);
    // Pool must remain usable after a task threw.
    auto h = pool.submit([] { return 1; });
    if (ready(h)) CHECK_EQ(h.get(), 1);
}

void test_submit_after_shutdown() {
    pce::ThreadPool pool(2);
    pool.shutdown();
    bool thrown = false;
    try {
        pool.submit([] { return 1; });
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    CHECK(thrown);
}

void test_graceful_shutdown_pending() {
    pce::ThreadPool pool(2);
    std::mutex m;
    std::condition_variable cv;
    bool release = false;
    std::atomic<int> done{0};
    std::vector<std::future<void>> fs;
    // Block both workers so the remaining tasks are definitely pending.
    std::atomic<int> blocked{0};
    for (int i = 0; i < 2; ++i) {
        fs.push_back(pool.submit([&] {
            blocked.fetch_add(1);
            std::unique_lock<std::mutex> lock(m);
            cv.wait_for(lock, kTimeout, [&] { return release; });
            done.fetch_add(1);
        }));
    }
    while (blocked.load() < 2) std::this_thread::yield();
    for (int i = 0; i < 100; ++i) fs.push_back(pool.submit([&done] { done.fetch_add(1); }));
    {
        std::lock_guard<std::mutex> lock(m);
        release = true;
    }
    cv.notify_all();
    pool.shutdown();  // must run all 102 accepted tasks before returning
    CHECK_EQ(done.load(), 102);
    for (auto& f : fs) CHECK(f.wait_for(0s) == std::future_status::ready);
}

void test_shutdown_multiple_times() {
    pce::ThreadPool pool(2);
    auto f = pool.submit([] { return 7; });
    pool.shutdown();
    pool.shutdown();
    pool.shutdown();
    CHECK_EQ(f.get(), 7);
}

void test_concurrent_shutdown() {
    for (int round = 0; round < 20; ++round) {
        pce::ThreadPool pool(4);
        std::atomic<int> done{0};
        for (int i = 0; i < 50; ++i) (void)pool.submit([&done] { done.fetch_add(1); });
        std::atomic<int> finished_after_all_done{0};
        std::vector<std::thread> callers;
        for (int i = 0; i < 6; ++i) {
            callers.emplace_back([&] {
                pool.shutdown();
                // Every caller must observe a fully drained pool on return.
                if (done.load() == 50) finished_after_all_done.fetch_add(1);
            });
        }
        for (auto& t : callers) t.join();
        CHECK_EQ(finished_after_all_done.load(), 6);
    }
}

void test_shutdown_from_worker_throws() {
    pce::ThreadPool pool(1);
    auto f = pool.submit([&pool] {
        try {
            pool.shutdown();
        } catch (const std::logic_error&) {
            return true;
        }
        return false;
    });
    if (ready(f)) CHECK(f.get());
}

void test_move_only() {
    pce::ThreadPool pool(2);
    // Move-only callable.
    auto up = std::make_unique<int>(10);
    auto f = pool.submit([p = std::move(up)] { return *p + 1; });
    // Move-only argument, taken by value.
    auto g = pool.submit([](std::unique_ptr<int> p) { return *p * 2; }, std::make_unique<int>(21));
    // Move-only argument consumed through rvalue reference parameter.
    auto h = pool.submit([](std::unique_ptr<int>&& p) { return *p; }, std::make_unique<int>(5));
    if (ready(f)) CHECK_EQ(f.get(), 11);
    if (ready(g)) CHECK_EQ(g.get(), 42);
    if (ready(h)) CHECK_EQ(h.get(), 5);
}

void test_args_copied_not_dangling() {
    pce::ThreadPool pool(2);
    std::future<std::string> f;
    {
        std::string local = "temporary";
        f = pool.submit([](std::string s) { return s + "-ok"; }, local);
        local = "mutated";
    }
    if (ready(f)) CHECK_EQ(f.get(), std::string("temporary-ok"));
}

void test_queue_close_wakes_waiters() {
    pce::TaskQueue q;
    constexpr int kWaiters = 6;
    std::atomic<int> waiting{0};
    std::atomic<int> woke{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kWaiters; ++i) {
        threads.emplace_back([&] {
            std::function<void()> t;
            waiting.fetch_add(1);
            if (!q.wait_and_pop(t)) woke.fetch_add(1);
        });
    }
    while (waiting.load() < kWaiters) std::this_thread::yield();
    q.close();
    for (auto& t : threads) t.join();  // hangs (and ctest times out) if close() fails to wake
    CHECK_EQ(woke.load(), kWaiters);
}

void test_idle_pool_shutdown_wakes_workers() {
    pce::ThreadPool pool(8);
    pool.shutdown();  // would hang forever if idle workers were not woken
    CHECK(true);
}

void test_repeated_construct_destroy() {
    for (int i = 0; i < 200; ++i) {
        pce::ThreadPool pool(1 + i % 4);
        if (i % 3 == 0) {
            auto f = pool.submit([i] { return i; });
            if (ready(f)) CHECK_EQ(f.get(), i);
        }
    }
}

void test_destructor_drains_tasks() {
    std::atomic<int> done{0};
    {
        pce::ThreadPool pool(2);
        for (int i = 0; i < 200; ++i) (void)pool.submit([&done] { done.fetch_add(1); });
    }
    CHECK_EQ(done.load(), 200);
}

void test_queue_fifo_and_drain() {
    pce::TaskQueue q;
    std::vector<int> order;
    for (int i = 0; i < 5; ++i) CHECK(q.push([&order, i] { order.push_back(i); }));
    CHECK(!q.is_closed());
    CHECK(!q.push(nullptr));
    q.close();
    q.close();  // idempotent
    CHECK(q.is_closed());
    CHECK(!q.push([] {}));  // rejected once closed
    CHECK_EQ(q.size(), std::size_t{5});

    std::function<void()> task;
    int popped = 0;
    while (q.wait_and_pop(task)) {  // drains queued tasks after close, then returns false
        task();
        ++popped;
    }
    CHECK_EQ(popped, 5);
    CHECK(order == (std::vector<int>{0, 1, 2, 3, 4}));
    CHECK(!q.wait_and_pop(task));  // closed and empty stays false
}

}  // namespace

int main() {
    const Test tests[] = {
        {"single_worker", test_single_worker},
        {"multiple_workers", test_multiple_workers},
        {"zero_threads_rejected", test_zero_threads_rejected},
        {"hundred_tasks", test_hundred_tasks},
        {"concurrent_producers", test_concurrent_producers},
        {"non_void_results", test_non_void_results},
        {"void_tasks", test_void_tasks},
        {"exception_propagation", test_exception_propagation},
        {"submit_after_shutdown", test_submit_after_shutdown},
        {"graceful_shutdown_pending", test_graceful_shutdown_pending},
        {"shutdown_multiple_times", test_shutdown_multiple_times},
        {"concurrent_shutdown", test_concurrent_shutdown},
        {"shutdown_from_worker_throws", test_shutdown_from_worker_throws},
        {"move_only", test_move_only},
        {"args_copied_not_dangling", test_args_copied_not_dangling},
        {"queue_close_wakes_waiters", test_queue_close_wakes_waiters},
        {"idle_pool_shutdown_wakes_workers", test_idle_pool_shutdown_wakes_workers},
        {"repeated_construct_destroy", test_repeated_construct_destroy},
        {"destructor_drains_tasks", test_destructor_drains_tasks},
        {"queue_fifo_and_drain", test_queue_fifo_and_drain},
    };

    int passed = 0;
    int total = 0;
    for (const auto& t : tests) {
        ++total;
        const int before = g_failures;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::cerr << "  unexpected exception: " << e.what() << '\n';
        }
        const bool ok = g_failures == before;
        if (ok) ++passed;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << t.name << std::endl;
    }
    std::cout << passed << '/' << total << " tests passed, " << g_checks << " checks\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
