// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_STATISTICS_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_STATISTICS_HPP

#include <atomic>

class Statistics {
  public:
    void Reset() noexcept {
        num_actions_queued_ = 0;
        num_actions_executed_ = 0;
        num_actions_cached_ = 0;
        num_actions_flaky_ = 0;
        num_actions_flaky_tainted_ = 0;
        num_rebuilt_actions_compared_ = 0;
        num_rebuilt_actions_missing_ = 0;
        num_trees_analysed_ = 0;
    }
    void IncrementActionsQueuedCounter() noexcept { ++num_actions_queued_; }
    void IncrementActionsExecutedCounter() noexcept { ++num_actions_executed_; }
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
    void IncrementExportsCachedCounter() noexcept { ++num_exports_cached_; }
    void IncrementExportsUncachedCounter() noexcept { ++num_exports_uncached_; }
    void IncrementExportsNotEligibleCounter() noexcept {
        ++num_exports_not_eligible_;
    }
    void IncrementTreesAnalysedCounter() noexcept { ++num_trees_analysed_; }
    [[nodiscard]] auto ActionsQueuedCounter() const noexcept -> int {
        return num_actions_queued_;
    }
    [[nodiscard]] auto ActionsExecutedCounter() const noexcept -> int {
        return num_actions_executed_;
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
    [[nodiscard]] auto ExportsCachedCounter() const noexcept -> int {
        return num_exports_cached_;
    }
    [[nodiscard]] auto ExportsUncachedCounter() const noexcept -> int {
        return num_exports_uncached_;
    }
    [[nodiscard]] auto ExportsNotEligibleCounter() const noexcept -> int {
        return num_exports_not_eligible_;
    }
    [[nodiscard]] auto TreesAnalysedCounter() const noexcept -> int {
        return num_trees_analysed_;
    }

  private:
    std::atomic<int> num_actions_queued_{};
    std::atomic<int> num_actions_executed_{};
    std::atomic<int> num_actions_cached_{};
    std::atomic<int> num_actions_flaky_{};
    std::atomic<int> num_actions_flaky_tainted_{};
    std::atomic<int> num_rebuilt_actions_missing_{};
    std::atomic<int> num_rebuilt_actions_compared_{};
    std::atomic<int> num_exports_cached_{};
    std::atomic<int> num_exports_uncached_{};
    std::atomic<int> num_exports_not_eligible_{};
    std::atomic<int> num_trees_analysed_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_STATISTICS_HPP
