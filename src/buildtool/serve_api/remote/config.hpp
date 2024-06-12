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
#include <vector>

#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/main/build_utils.hpp"

class RemoteServeConfig {
  public:
    // Obtain global instance
    [[nodiscard]] static auto Instance() noexcept -> RemoteServeConfig& {
        static RemoteServeConfig config;
        return config;
    }

    // Set remote execution and cache address, unsets if parsing `address` fails
    [[nodiscard]] auto SetRemoteAddress(std::string const& address) noexcept
        -> bool {
        return static_cast<bool>(remote_address_ = ParseAddress(address));
    }

    // Set the list of known repositories
    [[nodiscard]] auto SetKnownRepositories(
        std::vector<std::filesystem::path> const& repos) noexcept -> bool {
        repositories_ = std::vector<std::filesystem::path>(
            std::make_move_iterator(repos.begin()),
            std::make_move_iterator(repos.end()));
        return repos.size() == repositories_.size();
    }

    // Set the number of jobs
    [[nodiscard]] auto SetJobs(std::size_t jobs) noexcept -> bool {
        return static_cast<bool>(jobs_ = jobs);
    }

    // Set the number of build jobs
    [[nodiscard]] auto SetBuildJobs(std::size_t build_jobs) noexcept -> bool {
        return static_cast<bool>(build_jobs_ = build_jobs);
    }

    // Set the action timeout
    [[nodiscard]] auto SetActionTimeout(
        std::chrono::milliseconds const& timeout) noexcept -> bool {
        timeout_ = timeout;
        return timeout_ > std::chrono::seconds{0};
    }

    void SetTCStrategy(TargetCacheWriteStrategy strategy) noexcept {
        tc_strategy_ = strategy;
    }

    // Remote execution address, if set
    [[nodiscard]] auto RemoteAddress() const noexcept
        -> std::optional<ServerAddress> {
        return remote_address_;
    }

    // Repositories known to 'just serve'
    [[nodiscard]] auto KnownRepositories() const noexcept
        -> const std::vector<std::filesystem::path>& {
        return repositories_;
    }

    // Get the number of jobs
    [[nodiscard]] auto Jobs() const noexcept -> std::size_t { return jobs_; }

    // Get the number of build jobs
    [[nodiscard]] auto BuildJobs() const noexcept -> std::size_t {
        return build_jobs_;
    }

    // Get the action timeout
    [[nodiscard]] auto ActionTimeout() const noexcept
        -> std::chrono::milliseconds {
        return timeout_;
    }

    // Get the target-level cache write strategy
    [[nodiscard]] auto TCStrategy() const noexcept -> TargetCacheWriteStrategy {
        return tc_strategy_;
    }

  private:
    // Server address of remote execution.
    std::optional<ServerAddress> remote_address_{};

    // Known Git repositories to serve server.
    std::vector<std::filesystem::path> repositories_{};

    // Number of jobs
    std::size_t jobs_{};

    // Number of build jobs
    std::size_t build_jobs_{};

    // Action timeout
    std::chrono::milliseconds timeout_{};

    // Strategy for synchronizing target-level cache
    TargetCacheWriteStrategy tc_strategy_{TargetCacheWriteStrategy::Sync};
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_CONFIG_HPP
