#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "task_priority.hpp"

namespace pce {

// Immutable point-in-time snapshot of pool activity.
//
// Semantics:
//  - tasks_submitted: tasks successfully accepted by the pool. Rejected
//    submissions (after shutdown began) are never counted.
//  - tasks_started: tasks whose user callable began executing.
//  - tasks_completed: accepted tasks whose user callable finished, including
//    ones that threw. Counted only after the callable has returned/thrown.
//  - tasks_failed: tasks whose user callable threw (a subset of completed).
//  - *_priority_submitted: per-priority breakdown of tasks_submitted.
//  - active_workers: workers executing a task right now (idle workers are not
//    counted). This is NOT an operating-system CPU utilization figure.
//  - peak_active_workers: maximum simultaneous active_workers observed.
//  - total_execution_ms: sum of measured execution durations (steady_clock)
//    of completed tasks. Queue waiting time is excluded.
//  - average_execution_ms: total_execution_ms / tasks_completed (0 if none).
//
// Every snapshot is taken under one lock, so these hold in each snapshot:
//   tasks_completed <= tasks_started <= tasks_submitted
//   tasks_failed <= tasks_completed
//   active_workers == tasks_started - tasks_completed
//   active_workers <= peak_active_workers
//   high + normal + low submitted == tasks_submitted
struct ExecutionStats {
    std::uint64_t tasks_submitted = 0;
    std::uint64_t tasks_started = 0;
    std::uint64_t tasks_completed = 0;
    std::uint64_t tasks_failed = 0;

    std::uint64_t high_priority_submitted = 0;
    std::uint64_t normal_priority_submitted = 0;
    std::uint64_t low_priority_submitted = 0;

    std::size_t active_workers = 0;
    std::size_t peak_active_workers = 0;

    double total_execution_ms = 0.0;
    double average_execution_ms = 0.0;
};

// Internal thread-safe accumulator behind ThreadPool::get_stats().
//
// A single mutex protects all counters so snapshots are mutually consistent.
// The critical sections are a handful of integer operations. The lock is never
// held while user code runs and this class never calls out while locked, so it
// is always the innermost lock (lock order: queue mutex -> stats mutex).
class StatsRecorder {
public:
    StatsRecorder() = default;
    StatsRecorder(const StatsRecorder&) = delete;
    StatsRecorder& operator=(const StatsRecorder&) = delete;

    void on_submitted(TaskPriority priority) noexcept;
    void on_started() noexcept;
    void on_finished(std::chrono::nanoseconds duration, bool failed) noexcept;

    ExecutionStats snapshot() const;
    std::size_t active_workers() const;

private:
    mutable std::mutex mutex_;
    std::uint64_t submitted_ = 0;
    std::uint64_t started_ = 0;
    std::uint64_t completed_ = 0;
    std::uint64_t failed_ = 0;
    std::uint64_t high_ = 0;
    std::uint64_t normal_ = 0;
    std::uint64_t low_ = 0;
    std::size_t active_ = 0;
    std::size_t peak_ = 0;
    std::chrono::nanoseconds total_execution_{0};
};

// RAII guard placed around the user callable. Records start on construction
// and completion (with duration and failure flag) on destruction, so the
// active count is restored even if the callable throws.
class ExecutionScope {
public:
    explicit ExecutionScope(StatsRecorder& recorder) noexcept : recorder_(recorder) {
        recorder_.on_started();
        start_ = std::chrono::steady_clock::now();  // excludes stats-lock time
    }

    ~ExecutionScope() {
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        recorder_.on_finished(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed),
                              failed_);
    }

    ExecutionScope(const ExecutionScope&) = delete;
    ExecutionScope& operator=(const ExecutionScope&) = delete;

    void mark_failed() noexcept { failed_ = true; }

private:
    StatsRecorder& recorder_;
    std::chrono::steady_clock::time_point start_;
    bool failed_ = false;
};

}  // namespace pce
