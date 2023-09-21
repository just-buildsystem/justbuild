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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/common/remote/remote_common.hpp"

class RemoteExecutionConfig {
  public:
    // Obtain global instance
    [[nodiscard]] static auto Instance() noexcept -> RemoteExecutionConfig& {
        static RemoteExecutionConfig config;
        return config;
    }

    // Set remote execution and cache address, unsets if parsing `address` fails
    [[nodiscard]] static auto SetRemoteAddress(
        std::string const& address) noexcept -> bool {
        auto& inst = Instance();
        return static_cast<bool>(inst.remote_address_ = inst.cache_address_ =
                                     ParseAddress(address));
    }

    // Set remote-execution dispatch property list
    [[nodiscard]] static auto SetRemoteExecutionDispatch(
        const std::filesystem::path& filename) noexcept -> bool;

    // Set specific cache address, unsets if parsing `address` fails
    [[nodiscard]] static auto SetCacheAddress(
        std::string const& address) noexcept -> bool {
        return static_cast<bool>(Instance().cache_address_ =
                                     ParseAddress(address));
    }

    // Add platform property from string of form "key:val"
    [[nodiscard]] static auto AddPlatformProperty(
        std::string const& property) noexcept -> bool {
        if (auto pair = ParseProperty(property)) {
            Instance().platform_properties_[std::move(pair->first)] =
                std::move(pair->second);
            return true;
        }
        return false;
    }

    // Remote execution address, if set
    [[nodiscard]] static auto RemoteAddress() noexcept
        -> std::optional<ServerAddress> {
        return Instance().remote_address_;
    }

    // Cache address, if set
    [[nodiscard]] static auto CacheAddress() noexcept
        -> std::optional<ServerAddress> {
        return Instance().cache_address_;
    }

    // Instance dispatch list
    [[nodiscard]] static auto DispatchList() noexcept -> std::vector<
        std::pair<std::map<std::string, std::string>, ServerAddress>> {
        return Instance().dispatch_;
    }

    [[nodiscard]] static auto PlatformProperties() noexcept
        -> std::map<std::string, std::string> {
        return Instance().platform_properties_;
    }

  private:
    // Server address of remote execution.
    std::optional<ServerAddress> remote_address_{};

    // Server dispatch data
    std::vector<std::pair<std::map<std::string, std::string>, ServerAddress>>
        dispatch_{};

    // Server address of cache endpoint for rebuild.
    std::optional<ServerAddress> cache_address_{};

    // Platform properties for execution.
    std::map<std::string, std::string> platform_properties_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
