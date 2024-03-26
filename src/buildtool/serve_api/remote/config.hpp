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
    [[nodiscard]] static auto SetRemoteAddress(
        std::string const& address) noexcept -> bool {
        auto& inst = Instance();
        return static_cast<bool>(inst.remote_address_ = ParseAddress(address));
    }

    // Set the list of known repositories
    [[nodiscard]] static auto SetKnownRepositories(
        std::vector<std::filesystem::path> const& repos) noexcept -> bool {
        auto& inst = Instance();
        inst.repositories_ = std::vector<std::filesystem::path>(
            std::make_move_iterator(repos.begin()),
            std::make_move_iterator(repos.end()));
        return repos.size() == inst.repositories_.size();
    }

    // Set the number of jobs
    [[nodiscard]] static auto SetJobs(std::size_t jobs) noexcept -> bool {
        return static_cast<bool>(Instance().jobs_ = jobs);
    }

    // Set the number of build jobs
    [[nodiscard]] static auto SetBuildJobs(std::size_t build_jobs) noexcept
        -> bool {
        return static_cast<bool>(Instance().build_jobs_ = build_jobs);
    }

    // Set the action timeout
    [[nodiscard]] static auto SetActionTimeout(
        std::chrono::milliseconds const& timeout) noexcept -> bool {
        Instance().timeout_ = timeout;
        return Instance().timeout_ > std::chrono::seconds{0};
    }

    static void SetTCStrategy(TargetCacheWriteStrategy strategy) noexcept {
        Instance().tc_strategy_ = strategy;
    }

    // Remote execution address, if set
    [[nodiscard]] static auto RemoteAddress() noexcept
        -> std::optional<ServerAddress> {
        return Instance().remote_address_;
    }

    // Repositories known to 'just serve'
    [[nodiscard]] static auto KnownRepositories() noexcept
        -> const std::vector<std::filesystem::path>& {
        return Instance().repositories_;
    }

    // Get the number of jobs
    [[nodiscard]] static auto Jobs() noexcept -> std::size_t {
        return Instance().jobs_;
    }

    // Get the number of build jobs
    [[nodiscard]] static auto BuildJobs() noexcept -> std::size_t {
        return Instance().build_jobs_;
    }

    // Get the action timeout
    [[nodiscard]] static auto ActionTimeout() noexcept
        -> std::chrono::milliseconds {
        return Instance().timeout_;
    }

    // Get the target-level cache write strategy
    [[nodiscard]] static auto TCStrategy() noexcept
        -> TargetCacheWriteStrategy {
        return Instance().tc_strategy_;
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
