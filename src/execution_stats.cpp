#include "execution_stats.hpp"

namespace pce {

void StatsRecorder::on_submitted(TaskPriority priority) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++submitted_;
    switch (priority) {
        case TaskPriority::High:   ++high_;   break;
        case TaskPriority::Normal: ++normal_; break;
        case TaskPriority::Low:    ++low_;    break;
    }
}

void StatsRecorder::on_started() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++started_;
    ++active_;
    if (active_ > peak_) {
        peak_ = active_;
    }
}

void StatsRecorder::on_finished(std::chrono::nanoseconds duration, bool failed) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++completed_;
    if (failed) {
        ++failed_;
    }
    --active_;
    total_execution_ += duration;
}

void StatsRecorder::record_wait_time(std::chrono::nanoseconds wait_time) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    total_wait_ += wait_time;
}

void StatsRecorder::record_overhead(std::chrono::nanoseconds overhead) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    total_overhead_ += overhead;
}

void StatsRecorder::record_steal_attempt(bool success) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++steal_attempts_;
    if (success) {
        ++successful_steals_;
    }
}

ExecutionStats StatsRecorder::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ExecutionStats s;
    s.tasks_submitted = submitted_;
    s.tasks_started = started_;
    s.tasks_completed = completed_;
    s.tasks_failed = failed_;
    s.high_priority_submitted = high_;
    s.normal_priority_submitted = normal_;
    s.low_priority_submitted = low_;
    s.active_workers = active_;
    s.peak_active_workers = peak_;
    s.tasks_stolen = successful_steals_;
    s.steal_attempts = steal_attempts_;
    s.successful_steals = successful_steals_;
    
    s.total_execution_time_ms =
        std::chrono::duration<double, std::milli>(total_execution_).count();
    s.average_execution_time_ms =
        completed_ == 0 ? 0.0 : s.total_execution_time_ms / static_cast<double>(completed_);

    s.total_queue_wait_time_ms =
        std::chrono::duration<double, std::milli>(total_wait_).count();
    s.average_queue_wait_time_ms =
        started_ == 0 ? 0.0 : s.total_queue_wait_time_ms / static_cast<double>(started_);

    s.total_scheduler_overhead_ms =
        std::chrono::duration<double, std::milli>(total_overhead_).count();
    return s;
}

std::size_t StatsRecorder::active_workers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

}  // namespace pce
