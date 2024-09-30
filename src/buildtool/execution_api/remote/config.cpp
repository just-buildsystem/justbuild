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

#include "src/buildtool/execution_api/remote/config.hpp"

#include <exception>
#include <fstream>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto RemoteExecutionConfig::Builder::Build() const noexcept
    -> expected<RemoteExecutionConfig, std::string> {
    // To not duplicate default arguments in builder, create a default config
    // and copy arguments from there.
    RemoteExecutionConfig const default_config{};

    // Set remote endpoint.
    auto remote_address = default_config.remote_address;
    if (remote_address_raw_.has_value()) {
        remote_address = ParseAddress(*remote_address_raw_);
        if (not remote_address) {
            return unexpected{
                fmt::format("Failed to set remote endpoint address {}",
                            nlohmann::json(*remote_address_raw_).dump())};
        }
    }
    // Set cache endpoint.
    auto cache_address = default_config.cache_address;
    if (cache_address_raw_.has_value()) {
        cache_address = ParseAddress(*cache_address_raw_);
        // Cache endpoint can be in the usual "host:port" or the literal
        // "local", so we only fail if we cannot parse a non-"local" string,
        // because parsing a "local" literal will correctly return a nullopt.
        bool const is_parsed = cache_address.has_value();
        bool const is_local = *cache_address_raw_ == "local";
        if (not is_local and not is_parsed) {
            return unexpected{
                fmt::format("Failed to set cache endpoint address {}",
                            nlohmann::json(*cache_address_raw_).dump())};
        }
    }
    else {
        // If cache address not explicitly set, it defaults to remote address.
        cache_address = remote_address;
    }

    // Set dispatch info.
    auto dispatch = default_config.dispatch;
    if (dispatch_file_.has_value()) {
        try {
            if (auto dispatch_info =
                    FileSystemManager::ReadFile(*dispatch_file_)) {
                auto parsed = ParseDispatch(*dispatch_info);
                if (not parsed) {
                    return unexpected{parsed.error()};
                }
                dispatch = *std::move(parsed);
            }
            else {
                return unexpected{
                    fmt::format("Failed to read json file {}",
                                nlohmann::json(*dispatch_file_).dump())};
            }
        } catch (std::exception const& e) {
            return unexpected{fmt::format(
                "Assigning the endpoint configuration failed with:\n{}",
                e.what())};
        }
    }

    // Set platform properties.
    auto platform_properties = default_config.platform_properties;
    for (auto const& property : platform_properties_raw_) {
        if (auto pair = ParseProperty(property)) {
            try {
                platform_properties.insert_or_assign(pair->first, pair->second);
            } catch (std::exception const& e) {
                return unexpected{fmt::format("Failed to insert property {}",
                                              nlohmann::json(property).dump())};
            }
        }
        else {
            return unexpected{fmt::format("Adding platform property {} failed.",
                                          nlohmann::json(property).dump())};
        }
    }

    return RemoteExecutionConfig{
        .remote_address = std::move(remote_address),
        .dispatch = std::move(dispatch),
        .cache_address = std::move(cache_address),
        .platform_properties = std::move(platform_properties)};
}
