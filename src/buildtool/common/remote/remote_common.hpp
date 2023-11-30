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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_REMOTE_ADDRESS_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_REMOTE_ADDRESS_HPP

#include <optional>
#include <sstream>
#include <string>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/remote/port.hpp"

struct ServerAddress {
    std::string host{};
    Port port{};

    [[nodiscard]] auto ToJson() const noexcept -> nlohmann::json {
        return nlohmann::json{
            fmt::format("{}:{}", host, static_cast<std::uint16_t>(port))};
    }
};

[[nodiscard]] static auto ParseAddress(std::string const& address) noexcept
    -> std::optional<ServerAddress> {
    std::istringstream iss(address);
    std::string host;
    std::string port;
    if (not std::getline(iss, host, ':') or not std::getline(iss, port, ':')) {
        return std::nullopt;
    }
    auto port_num = ParsePort(port);
    if (not host.empty() and port_num) {
        return ServerAddress{std::move(host), *port_num};
    }
    return std::nullopt;
}

[[nodiscard]] static auto ParseProperty(std::string const& property) noexcept
    -> std::optional<std::pair<std::string, std::string>> {
    std::istringstream pss(property);
    std::string key;
    std::string val;
    if (not std::getline(pss, key, ':') or not std::getline(pss, val, ':')) {
        return std::nullopt;
    }
    return std::make_pair(key, val);
}

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_REMOTE_ADDRESS_HPP
