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
#include <optional>
#include <random>
#include <string>

#include "fmt/core.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/utils/cpp/expected.hpp"

inline constexpr unsigned int kDefaultInitialBackoffSeconds{1};
inline constexpr unsigned int kDefaultMaxBackoffSeconds{60};
inline constexpr unsigned int kDefaultAttempts{1};
inline constexpr auto kRetryLogLevel = LogLevel::Progress;

class RetryConfig final {
  public:
    class Builder;

    RetryConfig() = default;

    [[nodiscard]] auto GetMaxAttempts() const noexcept -> unsigned int {
        return attempts_;
    }

    /// \brief The waiting time is exponentially increased at each \p
    /// attempt until it exceeds max_backoff_seconds.
    ///
    /// To avoid overloading of the reachable resources, a jitter (aka,
    /// random value) is added to distribute the workload.
    [[nodiscard]] auto GetSleepTimeSeconds(unsigned int attempt) const noexcept
        -> unsigned int {
        auto backoff = initial_backoff_seconds_;
        // on the first attempt, we don't double the backoff time
        // also we do it in a for loop to avoid overflow
        for (auto x = 1U; x < attempt; ++x) {
            backoff <<= 1U;
            if (backoff >= max_backoff_seconds_) {
                backoff = max_backoff_seconds_;
                break;
            }
        }
        return backoff + Jitter(backoff);
    }

  private:
    unsigned int const initial_backoff_seconds_ = kDefaultInitialBackoffSeconds;
    unsigned int const max_backoff_seconds_ = kDefaultMaxBackoffSeconds;
    unsigned int const attempts_ = kDefaultAttempts;

    RetryConfig(unsigned int initial_backoff_seconds,
                unsigned int max_backoff_seconds,
                unsigned int attempts)
        : initial_backoff_seconds_{initial_backoff_seconds},
          max_backoff_seconds_{max_backoff_seconds},
          attempts_{attempts} {}

    using dist_type = std::uniform_int_distribution<std::mt19937::result_type>;

    [[nodiscard]] static auto Jitter(unsigned int backoff) noexcept ->
        typename dist_type::result_type {
        static std::mutex mutex;
        static std::mt19937 rng{std::random_device{}()};
        try {
            dist_type dist{0, 3UL * backoff};
            std::unique_lock lock(mutex);
            return dist(rng);
        } catch (...) {
            return 0;
        }
    }
};

class RetryConfig::Builder final {
  public:
    auto SetInitialBackoffSeconds(std::optional<unsigned int> x) noexcept
        -> Builder& {
        initial_backoff_seconds_ = x;
        return *this;
    }

    auto SetMaxBackoffSeconds(std::optional<unsigned int> x) noexcept
        -> Builder& {
        max_backoff_seconds_ = x;
        return *this;
    }

    auto SetMaxAttempts(std::optional<unsigned int> x) noexcept -> Builder& {
        attempts_ = x;
        return *this;
    }

    [[nodiscard]] auto Build() const noexcept
        -> expected<RetryConfig, std::string> {
        unsigned int initial_backoff_seconds = kDefaultInitialBackoffSeconds;
        if (initial_backoff_seconds_.has_value()) {
            if (*initial_backoff_seconds_ < 1) {
                return unexpected{
                    fmt::format("Invalid initial amount of seconds provided: "
                                "{}.\nValue must be strictly greater than 0.",
                                *initial_backoff_seconds_)};
            }
            initial_backoff_seconds = *initial_backoff_seconds_;
        }

        unsigned int max_backoff_seconds = kDefaultMaxBackoffSeconds;
        if (max_backoff_seconds_.has_value()) {
            if (*max_backoff_seconds_ < 1) {
                return unexpected{
                    fmt::format("Invalid max backoff provided: {}.\nValue must "
                                "be strictly greater than 0.",
                                *max_backoff_seconds_)};
            }
            max_backoff_seconds = *max_backoff_seconds_;
        }

        unsigned int attempts = kDefaultAttempts;
        if (attempts_.has_value()) {
            if (*attempts_ < 1) {
                return unexpected{
                    fmt::format("Invalid max number of attempts provided: "
                                "{}.\nValue must be strictly greater than 0.",
                                *attempts_)};
            }
            attempts = *attempts_;
        }

        return RetryConfig(
            initial_backoff_seconds, max_backoff_seconds, attempts);
    }

  private:
    std::optional<unsigned int> initial_backoff_seconds_;
    std::optional<unsigned int> max_backoff_seconds_;
    std::optional<unsigned int> attempts_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_PARAMETERS_HPP
