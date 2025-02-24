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
#include <compare>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <ratio>  // needed by durations
#include <string>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/main/build_utils.hpp"
#include "src/utils/cpp/expected.hpp"

struct RemoteServeConfig final {
    class Builder;

    // Server address of the serve endpoint.
    std::optional<ServerAddress> const remote_address;

    // Execution endpoint used by the client.
    std::optional<ServerAddress> const client_execution_address;

    // Known Git repositories to serve server.
    std::vector<std::filesystem::path> const known_repositories;

    // Number of jobs
    std::size_t const jobs = 0;

    // Number of build jobs
    std::size_t const build_jobs = 0;

    // Action timeout
    std::chrono::milliseconds const action_timeout{};

    // Strategy for synchronizing target-level cache
    TargetCacheWriteStrategy const tc_strategy{TargetCacheWriteStrategy::Sync};
};

class RemoteServeConfig::Builder final {
  public:
    // Set remote execution and cache address, unsets if parsing `address` fails
    auto SetClientExecutionAddress(std::optional<std::string> value) noexcept
        -> Builder& {
        client_execution_address_ = std::move(value);
        return *this;
    }

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
    [[nodiscard]] auto Build() const noexcept
        -> expected<RemoteServeConfig, std::string> {
        // To not duplicate default arguments of RemoteServeConfig in builder,
        // create a default config and copy default arguments from there.
        RemoteServeConfig const default_config{};

        auto remote_address = default_config.remote_address;
        if (remote_address_.has_value()) {
            remote_address = ParseAddress(*remote_address_);
            if (not remote_address) {
                return unexpected{
                    fmt::format("Setting serve service address '{}' failed.",
                                *remote_address_)};
            }
        }
        auto client_execution_address = default_config.client_execution_address;
        if (client_execution_address_.has_value()) {
            client_execution_address = ParseAddress(*client_execution_address_);
            if (not client_execution_address) {
                return unexpected{
                    fmt::format("Setting client execution address '{}' failed.",
                                *client_execution_address_)};
            }
        }
        auto known_repositories = default_config.known_repositories;
        if (known_repositories_.has_value()) {
            try {
                known_repositories = *known_repositories_;
            } catch (std::exception const& ex) {
                return unexpected{
                    std::string("Setting known repositories failed.")};
            }
        }

        auto jobs = default_config.jobs;
        if (jobs_.has_value()) {
            jobs = *jobs_;
            if (jobs == 0) {
                return unexpected{std::string{"Setting jobs failed."}};
            }
        }

        auto build_jobs = default_config.jobs;
        if (build_jobs_.has_value()) {
            build_jobs = *build_jobs_;
            if (build_jobs == 0) {
                return unexpected{std::string{"Setting build jobs failed."}};
            }
        }

        auto action_timeout = default_config.action_timeout;
        if (action_timeout_.has_value()) {
            action_timeout = *action_timeout_;
            if (bool const valid = action_timeout > std::chrono::seconds{0};
                not valid) {
                return unexpected{
                    std::string{"Setting action timeout failed."}};
            }
        }

        auto tc_strategy = default_config.tc_strategy;
        if (tc_strategy_.has_value()) {
            tc_strategy = *tc_strategy_;
        }

        return RemoteServeConfig{
            .remote_address = std::move(remote_address),
            .client_execution_address = std::move(client_execution_address),
            .known_repositories = std::move(known_repositories),
            .jobs = jobs,
            .build_jobs = build_jobs,
            .action_timeout = action_timeout,
            .tc_strategy = tc_strategy};
    }

  private:
    // Server address of the serve endpoint.
    std::optional<std::string> remote_address_;

    // Execution endpoint used by the client.
    std::optional<std::string> client_execution_address_;

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
