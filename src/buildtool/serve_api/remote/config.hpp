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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_CONFIG_HPP

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/main/build_utils.hpp"

struct RemoteServeConfig final {
    class Builder;

    // Obtain global instance
    [[nodiscard]] static auto Instance() noexcept -> RemoteServeConfig& {
        static RemoteServeConfig config;
        return config;
    }

    // Set remote execution and cache address, unsets if parsing `address` fails
    [[nodiscard]] auto SetRemoteAddress(std::string const& value) noexcept
        -> bool {
        return static_cast<bool>(remote_address = ParseAddress(value));
    }

    // Set the list of known repositories
    [[nodiscard]] auto SetKnownRepositories(
        std::vector<std::filesystem::path> const& value) noexcept -> bool {
        known_repositories = std::vector<std::filesystem::path>(
            std::make_move_iterator(value.begin()),
            std::make_move_iterator(value.end()));
        return value.size() == known_repositories.size();
    }

    // Set the number of jobs
    [[nodiscard]] auto SetJobs(std::size_t value) noexcept -> bool {
        return static_cast<bool>(jobs = value);
    }

    // Set the number of build jobs
    [[nodiscard]] auto SetBuildJobs(std::size_t value) noexcept -> bool {
        return static_cast<bool>(build_jobs = value);
    }

    // Set the action timeout
    [[nodiscard]] auto SetActionTimeout(
        std::chrono::milliseconds const& value) noexcept -> bool {
        action_timeout = value;
        return action_timeout > std::chrono::seconds{0};
    }

    void SetTCStrategy(TargetCacheWriteStrategy value) noexcept {
        tc_strategy = value;
    }

    // Remote execution address, if set
    [[nodiscard]] auto RemoteAddress() const noexcept
        -> std::optional<ServerAddress> {
        return remote_address;
    }

    // Repositories known to 'just serve'
    [[nodiscard]] auto KnownRepositories() const noexcept
        -> const std::vector<std::filesystem::path>& {
        return known_repositories;
    }

    // Get the number of jobs
    [[nodiscard]] auto Jobs() const noexcept -> std::size_t { return jobs; }

    // Get the number of build jobs
    [[nodiscard]] auto BuildJobs() const noexcept -> std::size_t {
        return build_jobs;
    }

    // Get the action timeout
    [[nodiscard]] auto ActionTimeout() const noexcept
        -> std::chrono::milliseconds {
        return action_timeout;
    }

    // Get the target-level cache write strategy
    [[nodiscard]] auto TCStrategy() const noexcept -> TargetCacheWriteStrategy {
        return tc_strategy;
    }

    // Server address of remote execution.
    std::optional<ServerAddress> remote_address{};

    // Known Git repositories to serve server.
    std::vector<std::filesystem::path> known_repositories{};

    // Number of jobs
    std::size_t jobs = 0;

    // Number of build jobs
    std::size_t build_jobs = 0;

    // Action timeout
    std::chrono::milliseconds action_timeout{};

    // Strategy for synchronizing target-level cache
    TargetCacheWriteStrategy tc_strategy{TargetCacheWriteStrategy::Sync};
};

class RemoteServeConfig::Builder final {
  public:
    // Set remote execution and cache address, unsets if parsing `address` fails
    auto SetRemoteAddress(std::optional<std::string> value) noexcept
        -> Builder& {
        remote_address_ = std::move(value);
        return *this;
    }

    // Set the list of known repositories
    auto SetKnownRepositories(std::vector<std::filesystem::path> value) noexcept
        -> Builder& {
        known_repositories_ = std::move(value);
        return *this;
    }

    // Set the number of jobs
    auto SetJobs(std::size_t value) noexcept -> Builder& {
        jobs_ = value;
        return *this;
    }

    // Set the number of build jobs
    auto SetBuildJobs(std::size_t value) noexcept -> Builder& {
        build_jobs_ = value;
        return *this;
    }

    // Set the action timeout
    auto SetActionTimeout(std::chrono::milliseconds const& value) noexcept
        -> Builder& {
        action_timeout_ = value;
        return *this;
    }

    auto SetTCStrategy(TargetCacheWriteStrategy value) noexcept -> Builder& {
        tc_strategy_ = value;
        return *this;
    }

    /// \brief Finalize building and create RemoteServeConfig.
    /// \return RemoteServeConfig on success or an error on failure.
    [[nodiscard]] auto Build() noexcept
        -> std::variant<RemoteServeConfig, std::string> {
        // To not duplicate default arguments of RemoteServeConfig in builder,
        // create a default config and copy default arguments from there.
        RemoteServeConfig const default_config;

        auto remote_address = default_config.remote_address;
        if (remote_address_.has_value()) {
            remote_address = ParseAddress(*remote_address_);
            if (not remote_address) {
                return fmt::format("Setting serve service address '{}' failed.",
                                   *remote_address_);
            }
        }

        auto known_repositories = default_config.known_repositories;
        if (known_repositories_.has_value()) {
            known_repositories = std::move(*known_repositories_);
        }

        auto jobs = default_config.jobs;
        if (jobs_.has_value()) {
            jobs = *jobs_;
            if (jobs == 0) {
                return "Setting jobs failed.";
            }
        }

        auto build_jobs = default_config.jobs;
        if (build_jobs_.has_value()) {
            build_jobs = *build_jobs_;
            if (build_jobs == 0) {
                return "Setting build jobs failed.";
            }
        }

        auto action_timeout = default_config.action_timeout;
        if (action_timeout_.has_value()) {
            action_timeout = *action_timeout_;
            if (bool const valid = action_timeout > std::chrono::seconds{0};
                not valid) {
                return "Setting action timeout failed.";
            }
        }

        auto tc_strategy = default_config.tc_strategy;
        if (tc_strategy_.has_value()) {
            tc_strategy = *tc_strategy_;
        }

        return RemoteServeConfig{
            .remote_address = std::move(remote_address),
            .known_repositories = std::move(known_repositories),
            .jobs = jobs,
            .build_jobs = build_jobs,
            .action_timeout = action_timeout,
            .tc_strategy = tc_strategy};
    }

  private:
    // Server address of remote execution.
    std::optional<std::string> remote_address_;

    // Known Git repositories to serve server.
    std::optional<std::vector<std::filesystem::path>> known_repositories_;

    // Number of jobs
    std::optional<std::size_t> jobs_;

    // Number of build jobs
    std::optional<std::size_t> build_jobs_;

    // Action timeout
    std::optional<std::chrono::milliseconds> action_timeout_;

    // Strategy for synchronizing target-level cache
    std::optional<TargetCacheWriteStrategy> tc_strategy_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_CONFIG_HPP
