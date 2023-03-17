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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_STATISTICS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_STATISTICS_HPP

#include <atomic>

class JustMRStatistics {
  public:
    [[nodiscard]] static auto Instance() noexcept -> JustMRStatistics& {
        static JustMRStatistics instance{};
        return instance;
    }

    void Reset() noexcept {
        num_local_paths_ = 0;
        num_cache_hits_ = 0;
        num_executed_ = 0;
    }
    void IncrementLocalPathsCounter() noexcept { ++num_local_paths_; }
    void IncrementCacheHitsCounter() noexcept { ++num_cache_hits_; }
    void IncrementExecutedCounter() noexcept { ++num_executed_; }

    [[nodiscard]] auto LocalPathsCounter() const noexcept -> int {
        return num_local_paths_;
    }
    [[nodiscard]] auto CacheHitsCounter() const noexcept -> int {
        return num_cache_hits_;
    }
    [[nodiscard]] auto ExecutedCounter() const noexcept -> int {
        return num_executed_;
    }

  private:
    std::atomic<int> num_local_paths_{};  // roots that are actual paths
    std::atomic<int> num_cache_hits_{};   // no-ops
    std::atomic<int> num_executed_{};     // actual work done
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_STATISTICS_HPP
