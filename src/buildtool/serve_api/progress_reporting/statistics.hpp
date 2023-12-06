// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_PROGRESS_REPORTING_STATISTICS_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_PROGRESS_REPORTING_STATISTICS_HPP

#include <atomic>

class ServeServiceStatistics {
  public:
    [[nodiscard]] static auto Instance() noexcept -> ServeServiceStatistics& {
        static ServeServiceStatistics instance{};
        return instance;
    }

    void Reset() noexcept {
        num_cache_hits_ = 0;
        num_dispatched_ = 0;
        num_served_ = 0;
    }
    void IncrementCacheHitsCounter() noexcept { ++num_cache_hits_; }
    void IncrementDispatchedCounter() noexcept { ++num_dispatched_; }
    void IncrementServedCounter() noexcept { ++num_served_; }

    [[nodiscard]] auto CacheHitsCounter() const noexcept -> int {
        return num_cache_hits_;
    }
    [[nodiscard]] auto DispatchedCounter() const noexcept -> int {
        return num_dispatched_;
    }
    [[nodiscard]] auto ServedCounter() const noexcept -> int {
        return num_served_;
    }

  private:
    // locally cached export targets
    std::atomic<int> num_cache_hits_{};
    // export targets for which we have queried just serve
    std::atomic<int> num_dispatched_{};
    // export targets for which just serve responded
    std::atomic<int> num_served_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_PROGRESS_REPORTING_STATISTICS_HPP
