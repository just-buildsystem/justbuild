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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_PARAMETERS_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_PARAMETERS_HPP

#include <mutex>
#include <random>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

constexpr unsigned int kDefaultInitialBackoffSeconds{1};
constexpr unsigned int kDefaultMaxBackoffSeconds{60};
constexpr unsigned int kDefaultAttempts{1};
constexpr auto kRetryLogLevel = LogLevel::Progress;
class Retry {
    using dist_type = std::uniform_int_distribution<std::mt19937::result_type>;

  public:
    Retry() = default;
    [[nodiscard]] static auto Instance() -> Retry& {
        static Retry instance{};
        return instance;
    }

    [[nodiscard]] static auto SetInitialBackoffSeconds(unsigned int x) noexcept
        -> bool {
        if (x < 1) {
            Logger::Log(
                LogLevel::Error,
                "Invalid initial amount of seconds provided: {}. Value must "
                "be strictly greater than 0.",
                x);
            return false;
        }
        Instance().initial_backoff_seconds_ = x;
        return true;
    }

    [[nodiscard]] static auto SetMaxBackoffSeconds(unsigned int x) noexcept
        -> bool {
        if (x < 1) {
            Logger::Log(LogLevel::Error,
                        "Invalid max backoff provided: {}. Value must be "
                        "strictly greater than 0.",
                        x);
            return false;
        }
        Instance().max_backoff_seconds_ = x;
        return true;
    }

    [[nodiscard]] static auto GetMaxBackoffSeconds() noexcept -> unsigned int {
        return Instance().max_backoff_seconds_;
    }

    [[nodiscard]] static auto SetMaxAttempts(unsigned int x) noexcept -> bool {
        if (x < 1) {
            Logger::Log(LogLevel::Error,
                        "Invalid number of max number of attempts provided: "
                        "{}. Value must be strictly greater than 0",
                        x);
            return false;
        }
        Instance().attempts_ = x;
        return true;
    }

    [[nodiscard]] static auto GetInitialBackoffSeconds() noexcept
        -> unsigned int {
        return Instance().initial_backoff_seconds_;
    }

    [[nodiscard]] static auto GetMaxAttempts() noexcept -> unsigned int {
        return Instance().attempts_;
    }

    [[nodiscard]] static auto Jitter(unsigned int backoff) noexcept ->
        typename dist_type::result_type {
        auto& inst = Instance();
        try {
            dist_type dist{0, backoff * 3};
            std::unique_lock lock(inst.mutex_);
            return dist(inst.rng_);
        } catch (...) {
            return 0;
        }
    }

    /// \brief The waiting time is exponentially increased at each \p attempt
    /// until it exceeds max_backoff_seconds.
    ///
    /// To avoid overloading of the reachable resources, a jitter (aka, random
    /// value) is added to distributed the workload.
    [[nodiscard]] static auto GetSleepTimeSeconds(unsigned int attempt) noexcept
        -> unsigned int {
        auto backoff = Retry::GetInitialBackoffSeconds();
        auto const& max_backoff = Retry::GetMaxBackoffSeconds();
        // on the first attempt, we don't double the backoff time
        // also we do it in a for loop to avoid overflow
        for (auto x = 1U; x < attempt; ++x) {
            backoff <<= 1U;
            if (backoff >= max_backoff) {
                backoff = max_backoff;
                break;
            }
        }
        return backoff + Retry::Jitter(backoff);
    }

  private:
    unsigned int initial_backoff_seconds_{kDefaultInitialBackoffSeconds};
    unsigned int max_backoff_seconds_{kDefaultMaxBackoffSeconds};
    unsigned int attempts_{kDefaultAttempts};
    LogLevel retry_log_level_{kRetryLogLevel};
    std::mutex mutex_;
    std::random_device dev_;
    std::mt19937 rng_{dev_()};
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_PARAMETERS_HPP
