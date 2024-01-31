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

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/remote/port.hpp"

struct ServerAddress {
    std::string host{};
    Port port{};

    [[nodiscard]] auto ToJson() const noexcept -> nlohmann::json {
        return nlohmann::json(
            fmt::format("{}:{}", host, static_cast<std::uint16_t>(port)));
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

[[nodiscard]] static auto
ParseDispatch(std::string const& dispatch_info) noexcept -> std::variant<
    std::string,
    std::vector<std::pair<std::map<std::string, std::string>, ServerAddress>>> {
    using result_t =
        std::variant<std::string,
                     std::vector<std::pair<std::map<std::string, std::string>,
                                           ServerAddress>>>;
    nlohmann::json dispatch;
    try {
        dispatch = nlohmann::json::parse(dispatch_info);
    } catch (std::exception const& e) {
        return fmt::format("Failed to parse endpoint configuration: {}",
                           e.what());
    }
    std::vector<std::pair<std::map<std::string, std::string>, ServerAddress>>
        parsed{};
    try {
        if (not dispatch.is_array()) {
            return fmt::format(
                "Endpoint configuration has to be a "
                "list of pairs, but found {}",
                dispatch.dump());
        }
        for (auto const& entry : dispatch) {
            if (not(entry.is_array() and entry.size() == 2)) {
                return fmt::format(
                    "Endpoint configuration has to be a list of "
                    "pairs, but found entry {}",
                    entry.dump());
            }
            if (not entry[0].is_object()) {
                return fmt::format(
                    "Property condition has to be "
                    "given as an object, but found {}",
                    entry[0].dump());
            }
            std::map<std::string, std::string> props{};
            for (auto const& [k, v] : entry[0].items()) {
                if (not v.is_string()) {
                    return fmt::format(
                        "Property condition has to be given as an "
                        "object of strings but found {}",
                        entry[0].dump());
                }
                props.emplace(k, v.template get<std::string>());
            }
            if (not entry[1].is_string()) {
                return fmt::format(
                    "Endpoint has to be specified as string (in "
                    "the form host:port), but found {}",
                    entry[1].dump());
            }
            auto endpoint = ParseAddress(entry[1].template get<std::string>());
            if (not endpoint) {
                return fmt::format("Failed to parse {} as endpoint.",
                                   entry[1].dump());
            }
            parsed.emplace_back(props, *endpoint);
        }
    } catch (std::exception const& e) {
        return fmt::format("Failure analysing endpoint configuration {}: {}",
                           dispatch.dump(),
                           e.what());
    }
    // success!
    return parsed;
}

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_REMOTE_ADDRESS_HPP
