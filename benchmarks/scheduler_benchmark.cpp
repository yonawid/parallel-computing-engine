#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>
#include <string>
#include <cassert>

#include "parallel_algorithms.hpp"
#include "thread_pool.hpp"

using namespace pce;

struct BenchmarkResult {
    std::string policy_name;
    std::size_t workers;
    std::size_t tasks_submitted;
    std::size_t tasks_completed;
    std::size_t tasks_verified;
    double execution_time_ms;
    double average_wait_time_ms;
    double total_overhead_ms;
    std::uint64_t steal_attempts;
    std::uint64_t successful_steals;
};

void run_matrix_benchmark(std::size_t num_tasks, std::size_t workers, SchedulingPolicy policy, std::vector<BenchmarkResult>& results) {
    std::string policy_name;
    switch (policy) {
        case SchedulingPolicy::FIFO: policy_name = "FIFO"; break;
        case SchedulingPolicy::Priority: policy_name = "Priority"; break;
        case SchedulingPolicy::WorkStealing: policy_name = "WorkStealing"; break;
    }

    std::cout << "Running " << num_tasks << " matrix mults on " << workers << " workers (" << policy_name << ")...\n";

    ThreadPool pool(workers, policy);

    // Generate deterministic matrices
    std::vector<std::vector<double>> A(64, std::vector<double>(64, 1.0));
    std::vector<std::vector<double>> B(64, std::vector<double>(64, 2.0));
    
    std::vector<std::future<std::vector<std::vector<double>>>> futures;
    futures.reserve(num_tasks);

    auto start_time = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < num_tasks; ++i) {
        // Vary priorities deterministically to stress Priority scheduler
        TaskPriority p = TaskPriority::Normal;
        if (i % 3 == 0) p = TaskPriority::High;
        else if (i % 5 == 0) p = TaskPriority::Low;

        futures.push_back(pool.submit_with_priority(p, [&A, &B]() {
            std::vector<std::vector<double>> C(64, std::vector<double>(64, 0.0));
            for (std::size_t r = 0; r < 64; ++r) {
                for (std::size_t k = 0; k < 64; ++k) {
                    for (std::size_t c = 0; c < 64; ++c) {
                        C[r][c] += A[r][k] * B[k][c];
                    }
                }
            }
            return C;
        }));
    }

    std::size_t completed = 0;
    std::size_t verified = 0;

    // Wait for all and verify
    for (auto& f : futures) {
        auto C = f.get();
        completed++;
        bool ok = true;
        for (std::size_t r = 0; r < 64; ++r) {
            for (std::size_t c = 0; c < 64; ++c) {
                if (C[r][c] != 128.0) {
                    ok = false;
                }
            }
        }
        if (ok) verified++;
    }
    
    if (verified != num_tasks) {
        std::cerr << "Validation failed! Verified: " << verified << " / " << num_tasks << "\n";
        std::exit(1);
    }

    auto end_time = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    pool.shutdown();
    auto stats = pool.get_stats();

    results.push_back({
        policy_name,
        workers,
        num_tasks,
        completed,
        verified,
        total_ms,
        stats.average_queue_wait_time_ms,
        stats.total_scheduler_overhead_ms,
        stats.steal_attempts,
        stats.successful_steals
    });
}

int main() {
    std::cout << "--- Scheduler Benchmark Correctness Verification ---\n";
    std::vector<BenchmarkResult> dummy_results;
    run_matrix_benchmark(100, 2, SchedulingPolicy::FIFO, dummy_results);
    run_matrix_benchmark(100, 2, SchedulingPolicy::Priority, dummy_results);
    run_matrix_benchmark(100, 2, SchedulingPolicy::WorkStealing, dummy_results);
    std::cout << "Correctness run passed!\n\n";

    std::vector<BenchmarkResult> results;
    std::vector<std::size_t> worker_counts = {1, 2, 4, 6};
    std::size_t full_tasks = 12000;

    std::cout << "--- Starting Full Scheduler Benchmark (" << full_tasks << " tasks) ---\n";
    for (auto workers : worker_counts) {
        run_matrix_benchmark(full_tasks, workers, SchedulingPolicy::FIFO, results);
        run_matrix_benchmark(full_tasks, workers, SchedulingPolicy::Priority, results);
        run_matrix_benchmark(full_tasks, workers, SchedulingPolicy::WorkStealing, results);
    }

    // Save to CSV
    std::ofstream csv("scheduler_benchmark.csv");
    csv << "Policy,Workers,TasksSubmitted,TasksCompleted,TasksVerified,TotalTimeMs,AvgWaitMs,OverheadMs,StealAttempts,SuccessfulSteals\n";
    for (const auto& r : results) {
        csv << r.policy_name << ","
            << r.workers << ","
            << r.tasks_submitted << ","
            << r.tasks_completed << ","
            << r.tasks_verified << ","
            << r.execution_time_ms << ","
            << r.average_wait_time_ms << ","
            << r.total_overhead_ms << ","
            << r.steal_attempts << ","
            << r.successful_steals << "\n";
    }
    std::cout << "Saved results to scheduler_benchmark.csv\n";

    return 0;
}
