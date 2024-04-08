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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BASE_PROGRESS_REPORTER_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BASE_PROGRESS_REPORTER_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>

// Type of a progress reporter. The reporter may only block in such a way that
// it return on a notification of the condition variable; moreover, it has to
// exit once the boolean is true.
using progress_reporter_t =
    std::function<void(std::atomic<bool>*, std::condition_variable*)>;

class BaseProgressReporter {
  public:
    [[nodiscard]] static auto Reporter(
        std::function<void(void)> report) noexcept -> progress_reporter_t;

  private:
    constexpr static std::int64_t kStartDelayMillis = 3000;
    // Scaling is roughly sqrt(2)
    constexpr static std::int64_t kDelayScalingFactorNumerator = 99;
    constexpr static std::int64_t kDelayScalingFactorDenominator = 70;
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BASE_PROGRESS_REPORTER_HPP
