// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/storage/backend_description.hpp"

#include <exception>

#include "fmt/core.h"
#include "nlohmann/json.hpp"

auto DescribeBackend(std::optional<ServerAddress> const& address,
                     ExecutionProperties const& properties,
                     std::vector<DispatchEndpoint> const& dispatch) noexcept
    -> expected<std::string, std::string> {
    nlohmann::json description;
    try {
        description["remote_address"] =
            address ? address->ToJson() : nlohmann::json{};
        description["platform_properties"] = properties;
    } catch (std::exception const& e) {
        return unexpected{
            fmt::format("Failed to serialize remote address and "
                        "platform_properties:\n{}",
                        e.what())};
    }

    if (not dispatch.empty()) {
        try {
            // only add the dispatch list, if not empty, so that keys remain
            // not only more readable, but also backwards compatible with
            // earlier versions.
            auto dispatch_list = nlohmann::json::array();
            for (auto const& [props, endpoint] : dispatch) {
                auto entry = nlohmann::json::array();
                entry.push_back(nlohmann::json(props));
                entry.push_back(endpoint.ToJson());
                dispatch_list.push_back(entry);
            }
            description["endpoint dispatch list"] = dispatch_list;
        } catch (std::exception const& e) {
            return unexpected{fmt::format(
                "Failed to serialize endpoint dispatch list:\n{}", e.what())};
        }
    }
    try {
        // json::dump with json::error_handler_t::replace will not throw an
        // exception if invalid UTF-8 sequences are detected in the input.
        // Instead, it will replace them with the UTF-8 replacement
        // character, but still it needs to be inside a try-catch clause to
        // ensure the noexcept modifier of the enclosing function.
        return description.dump(
            2, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (std::exception const& e) {
        return unexpected{fmt::format(
            "Failed to dump backend description to JSON:\n{}", e.what())};
    }
}
