#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_STATISTICS_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_STATISTICS_HPP

#include <atomic>

class Statistics {
  public:
    [[nodiscard]] static auto Instance() noexcept -> Statistics& {
        static Statistics instance{};
        return instance;
    }

    void Reset() noexcept {
        num_actions_queued_ = 0;
        num_actions_cached_ = 0;
        num_actions_flaky_ = 0;
        num_actions_flaky_tainted_ = 0;
        num_rebuilt_actions_compared_ = 0;
        num_rebuilt_actions_missing_ = 0;
    }
    void IncrementActionsQueuedCounter() noexcept { ++num_actions_queued_; }
    void IncrementActionsCachedCounter() noexcept { ++num_actions_cached_; }
    void IncrementActionsFlakyCounter() noexcept { ++num_actions_flaky_; }
    void IncrementActionsFlakyTaintedCounter() noexcept {
        ++num_actions_flaky_tainted_;
    }
    void IncrementRebuiltActionMissingCounter() noexcept {
        ++num_rebuilt_actions_missing_;
    }
    void IncrementRebuiltActionComparedCounter() noexcept {
        ++num_rebuilt_actions_compared_;
    }
    [[nodiscard]] auto ActionsQueuedCounter() const noexcept -> int {
        return num_actions_queued_;
    }
    [[nodiscard]] auto ActionsCachedCounter() const noexcept -> int {
        return num_actions_cached_;
    }
    [[nodiscard]] auto ActionsFlakyCounter() const noexcept -> int {
        return num_actions_flaky_;
    }
    [[nodiscard]] auto ActionsFlakyTaintedCounter() const noexcept -> int {
        return num_actions_flaky_tainted_;
    }
    [[nodiscard]] auto RebuiltActionMissingCounter() const noexcept -> int {
        return num_rebuilt_actions_missing_;
    }
    [[nodiscard]] auto RebuiltActionComparedCounter() const noexcept -> int {
        return num_rebuilt_actions_compared_;
    }

  private:
    std::atomic<int> num_actions_queued_{};
    std::atomic<int> num_actions_cached_{};
    std::atomic<int> num_actions_flaky_{};
    std::atomic<int> num_actions_flaky_tainted_{};
    std::atomic<int> num_rebuilt_actions_missing_{};
    std::atomic<int> num_rebuilt_actions_compared_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_STATISTICS_HPP
