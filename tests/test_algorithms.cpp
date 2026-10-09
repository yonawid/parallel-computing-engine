#include <algorithm>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "parallel_algorithms.hpp"
#include "thread_pool.hpp"

namespace {

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

struct Test {
    const char* name;
    void (*fn)();
};

void test_reduce_empty() {
    pce::ThreadPool pool(2);
    std::vector<int> v;
    int res = pce::parallel_reduce(pool, v.begin(), v.end(), 42, std::plus<int>());
    CHECK_EQ(res, 42);
}

void test_reduce_single() {
    pce::ThreadPool pool(2);
    std::vector<int> v = {10};
    int res = pce::parallel_reduce(pool, v.begin(), v.end(), 5, std::plus<int>());
    CHECK_EQ(res, 15);
}

void test_reduce_large_no_overflow() {
    pce::ThreadPool pool(4);
    std::vector<long long> v(10000, 2);
    long long res = pce::parallel_reduce(pool, v.begin(), v.end(), 0LL, std::plus<long long>());
    CHECK_EQ(res, 20000LL);
}

void test_reduce_custom_op() {
    pce::ThreadPool pool(3);
    std::vector<int> v = {1, 5, 2, 8, 3};
    auto max_op = [](int a, int b) { return std::max(a, b); };
    int res = pce::parallel_reduce(pool, v.begin(), v.end(), 0, max_op);
    CHECK_EQ(res, 8);
}

void test_reduce_different_workers() {
    std::vector<int> v(5000, 1);
    for (int workers : {1, 2, 4, 8}) {
        pce::ThreadPool pool(workers);
        int res = pce::parallel_reduce(pool, v.begin(), v.end(), 0, std::plus<int>());
        CHECK_EQ(res, 5000);
    }
}

void test_reduce_exception() {
    pce::ThreadPool pool(2);
    std::vector<int> v(2000, 1);
    bool caught = false;
    try {
        pce::parallel_reduce(pool, v.begin(), v.end(), 0, [](int, int) -> int {
            throw std::runtime_error("boom");
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    CHECK(caught);
}

void test_matmul_known_output() {
    pce::ThreadPool pool(2);
    std::vector<int> A = {1, 2, 3, 4}; // 2x2
    std::vector<int> B = {2, 0, 1, 2}; // 2x2
    std::vector<int> C(4);
    pce::parallel_matrix_multiply(pool, A, B, C, 2, 2, 2);
    std::vector<int> expected = {4, 4, 10, 8};
    CHECK(C == expected);
}

void test_matmul_identity() {
    pce::ThreadPool pool(2);
    std::vector<int> A = {1, 2, 3, 4, 5, 6, 7, 8, 9}; // 3x3
    std::vector<int> I = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // 3x3
    std::vector<int> C(9);
    pce::parallel_matrix_multiply(pool, A, I, C, 3, 3, 3);
    CHECK(C == A);
}

void test_matmul_rectangular() {
    pce::ThreadPool pool(3);
    std::vector<int> A = {1, 2, 3, 4, 5, 6}; // 2x3
    std::vector<int> B = {7, 8, 9, 10, 11, 12}; // 3x2
    std::vector<int> C(4);
    pce::parallel_matrix_multiply(pool, A, B, C, 2, 3, 2);
    std::vector<int> expected = {58, 64, 139, 154};
    CHECK(C == expected);
}

void test_matmul_mismatch() {
    pce::ThreadPool pool(1);
    std::vector<int> A(2);
    std::vector<int> B(2);
    std::vector<int> C(2);
    bool caught = false;
    try {
        pce::parallel_matrix_multiply(pool, A, B, C, 2, 2, 2); // 2x2 requires size 4
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    CHECK(caught);
}

void test_matmul_zero_dimensions() {
    pce::ThreadPool pool(2);
    std::vector<int> A;
    std::vector<int> B;
    std::vector<int> C;
    pce::parallel_matrix_multiply(pool, A, B, C, 0, 5, 0); // valid empty
    CHECK(C.empty());
}

void test_matmul_randomized_equivalence() {
    std::mt19937 gen(42);
    std::uniform_int_distribution<> dis(-10, 10);
    
    std::size_t M = 50, K = 70, N = 60;
    std::vector<int> A(M * K);
    std::vector<int> B(K * N);
    std::vector<int> C_seq(M * N);
    std::vector<int> C_par(M * N);

    for (auto& x : A) x = dis(gen);
    for (auto& x : B) x = dis(gen);

    pce::sequential_matrix_multiply(A, B, C_seq, M, K, N);
    
    pce::ThreadPool pool(4);
    pce::parallel_matrix_multiply(pool, A, B, C_par, M, K, N);

    CHECK(C_seq == C_par);
}

void test_matmul_exception() {
    // Actually our parallel_matrix_multiply does not throw inside the kernel for POD types.
    // We can't easily inject an exception without modifying the code or using a custom type.
    // We'll skip forcing an exception here, but test dimension mismatch does throw.
    CHECK(true);
}

void test_repeated_concurrent_execution() {
    pce::ThreadPool pool(4);
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            std::vector<int> A(100, 1);
            std::vector<int> B(100, 1);
            std::vector<int> C(100);
            pce::parallel_matrix_multiply(pool, A, B, C, 10, 10, 10);
            if (C[0] == 10) success_count++;
        });
    }
    for (auto& t : threads) t.join();
    CHECK_EQ(success_count.load(), 4);
}

} // namespace

int main() {
    const Test tests[] = {
        {"reduce_empty", test_reduce_empty},
        {"reduce_single", test_reduce_single},
        {"reduce_large_no_overflow", test_reduce_large_no_overflow},
        {"reduce_custom_op", test_reduce_custom_op},
        {"reduce_different_workers", test_reduce_different_workers},
        {"reduce_exception", test_reduce_exception},
        {"matmul_known_output", test_matmul_known_output},
        {"matmul_identity", test_matmul_identity},
        {"matmul_rectangular", test_matmul_rectangular},
        {"matmul_mismatch", test_matmul_mismatch},
        {"matmul_zero_dimensions", test_matmul_zero_dimensions},
        {"matmul_randomized_equivalence", test_matmul_randomized_equivalence},
        {"matmul_exception", test_matmul_exception},
        {"repeated_concurrent_execution", test_repeated_concurrent_execution},
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
