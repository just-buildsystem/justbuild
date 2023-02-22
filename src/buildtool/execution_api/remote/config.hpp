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
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/type_safe_arithmetic.hpp"

// Port
struct PortTag : type_safe_arithmetic_tag<std::uint16_t> {};
using Port = type_safe_arithmetic<PortTag>;

[[nodiscard]] static auto ParsePort(int const port_num) noexcept
    -> std::optional<Port> {
    try {
        static constexpr int kMaxPortNumber{
            std::numeric_limits<uint16_t>::max()};
        if (port_num >= 0 and port_num <= kMaxPortNumber) {
            return gsl::narrow_cast<Port::value_t>(port_num);
        }
    } catch (std::out_of_range const& e) {
        Logger::Log(LogLevel::Error, "Port raised out_of_range exception.");
    }
    return std::nullopt;
}

[[nodiscard]] static auto ParsePort(std::string const& port) noexcept
    -> std::optional<Port> {
    try {
        auto port_num = std::stoi(port);
        return ParsePort(port_num);
    } catch (std::invalid_argument const& e) {
        Logger::Log(LogLevel::Error, "Port raised invalid_argument exception.");
    }
    return std::nullopt;
}

class RemoteExecutionConfig {
  public:
    struct ServerAddress {
        std::string host{};
        Port port{};
    };

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

    [[nodiscard]] static auto PlatformProperties() noexcept
        -> std::map<std::string, std::string> {
        return Instance().platform_properties_;
    }

  private:
    // Server address of remote execution.
    std::optional<ServerAddress> remote_address_{};

    // Server address of cache endpoint for rebuild.
    std::optional<ServerAddress> cache_address_{};

    // Platform properties for execution.
    std::map<std::string, std::string> platform_properties_{};

    [[nodiscard]] static auto ParseAddress(std::string const& address) noexcept
        -> std::optional<ServerAddress> {
        std::istringstream iss(address);
        std::string host;
        std::string port;
        if (not std::getline(iss, host, ':') or
            not std::getline(iss, port, ':')) {
            return std::nullopt;
        }
        auto port_num = ParsePort(port);
        if (not host.empty() and port_num) {
            return ServerAddress{std::move(host), *port_num};
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto ParseProperty(
        std::string const& property) noexcept
        -> std::optional<std::pair<std::string, std::string>> {
        std::istringstream pss(property);
        std::string key;
        std::string val;
        if (not std::getline(pss, key, ':') or
            not std::getline(pss, val, ':')) {
            return std::nullopt;
        }
        return std::make_pair(key, val);
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
