#pragma once

#include <cstddef>

namespace pce {

// Scheduling priority. Higher priorities are dequeued first (strict priority).
// Numeric values are used as queue indices: Low = 0, Normal = 1, High = 2.
enum class TaskPriority {
    Low,
    Normal,
    High
};

inline constexpr std::size_t kPriorityLevels = 3;

constexpr std::size_t priority_index(TaskPriority priority) noexcept {
    return static_cast<std::size_t>(priority);
}

}  // namespace pce
