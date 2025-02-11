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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_MIRRORS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_MIRRORS_HPP

#include <algorithm>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/other_tools/utils/curl_url_handle.hpp"

struct Mirrors {
    nlohmann::json local_mirrors;        // maps URLs to list of local mirrors
    nlohmann::json preferred_hostnames;  // list of mirror hostnames
    nlohmann::json extra_inherit_env;
};

using MirrorsPtr = std::shared_ptr<Mirrors>;

namespace MirrorsUtils {

/// \brief Get the list of local mirrors for given primary URL.
[[nodiscard]] static inline auto GetLocalMirrors(
    MirrorsPtr const& additional_mirrors,
    std::string const& repo_url) noexcept -> std::vector<std::string> {
    try {
        if (additional_mirrors->local_mirrors.contains(repo_url)) {
            auto const& arr = additional_mirrors->local_mirrors[repo_url];
            if (arr.is_array()) {
                std::vector<std::string> res{};
                res.reserve(arr.size());
                for (auto const& [_, val] : arr.items()) {
                    if (val.is_string()) {
                        res.emplace_back(val.get<std::string>());
                    }
                    else {
                        Logger::Log(LogLevel::Warning,
                                    "Retrieving additional mirrors: found "
                                    "non-string list entry {}",
                                    val.dump());
                        return {};
                    }
                }
                return res;
            }
            Logger::Log(
                LogLevel::Warning,
                "Retrieving additional mirrors: found non-list value {}",
                arr.dump());
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Warning,
                    "Retrieving additional mirrors failed with:\n{}",
                    ex.what());
    }
    return {};
}

/// \brief Get the list of preferred hostnames for non-local fetches.
[[nodiscard]] static inline auto GetPreferredHostnames(
    MirrorsPtr const& additional_mirrors) noexcept -> std::vector<std::string> {
    try {
        std::vector<std::string> res{};
        res.reserve(additional_mirrors->preferred_hostnames.size());
        for (auto const& [_, val] :
             additional_mirrors->preferred_hostnames.items()) {
            if (val.is_string()) {
                res.emplace_back(val.get<std::string>());
            }
            else {
                Logger::Log(LogLevel::Warning,
                            "Retrieving preferred mirrors: found non-string "
                            "list entry {}",
                            val.dump());
                return {};
            }
        }
        return res;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Warning,
                    "Retrieving preferred mirrors failed with:\n{}",
                    ex.what());
        return {};
    };
}

/// \brief Get the list of extra variables to inherit.
[[nodiscard]] static inline auto GetInheritEnv(
    MirrorsPtr const& additional_mirrors,
    std::vector<std::string> const& base) noexcept -> std::vector<std::string> {
    try {
        std::vector<std::string> res(base);
        res.reserve(additional_mirrors->extra_inherit_env.size() + base.size());
        for (auto const& [_, val] :
             additional_mirrors->extra_inherit_env.items()) {
            if (val.is_string()) {
                res.emplace_back(val.get<std::string>());
            }
            else {
                Logger::Log(
                    LogLevel::Warning,
                    "Retrieving extra variables to inherit: found non-string "
                    "list entry {}",
                    val.dump());
            }
        }
        return res;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Warning,
                    "Retrieving extra environment variables to inherit failed "
                    "with:\n{}",
                    ex.what());
        return {};
    };
}

/// \brief Sort mirrors by the order of given hostnames.
[[nodiscard]] static inline auto SortByHostname(
    std::vector<std::string> const& mirrors,
    std::vector<std::string> const& hostnames) -> std::vector<std::string> {
    using map_t = std::unordered_map<std::string, std::vector<std::string>>;
    map_t mirrors_by_hostname{};
    mirrors_by_hostname.reserve(hostnames.size() + 1);

    // initialize map with known hostnames
    std::transform(
        hostnames.begin(),
        hostnames.end(),
        std::inserter(mirrors_by_hostname, mirrors_by_hostname.end()),
        [](auto const& hostname) ->
        typename map_t::value_type { return {hostname, {/*empty*/}}; });

    // fill mirrors list per hostname
    for (auto const& mirror : mirrors) {
        auto hostname =
            CurlURLHandle::GetHostname(mirror).value_or(std::string{});
        auto it = mirrors_by_hostname.find(hostname);
        if (it != mirrors_by_hostname.end()) {
            mirrors_by_hostname.at(hostname).emplace_back(mirror);
        }
        else {
            // add missing or unknown hostnames to fallback list with key ""
            mirrors_by_hostname[""].emplace_back(mirror);
        }
    }

    std::vector<std::string> ordered{};
    ordered.reserve(mirrors.size());

    // first, add mirrors in the order defined by hostnames
    for (auto const& hostname : hostnames) {
        auto it = mirrors_by_hostname.find(hostname);
        if (it != mirrors_by_hostname.end()) {
            auto& list = it->second;
            ordered.insert(ordered.end(),
                           std::make_move_iterator(list.begin()),
                           std::make_move_iterator(list.end()));
            // clear for safe revisit in case hostnames contains any duplicates
            list.clear();
        }
    }

    // second, append remaining mirrors from fallback list with key ""
    auto it = mirrors_by_hostname.find("");
    if (it != mirrors_by_hostname.end()) {
        auto& list = it->second;
        ordered.insert(ordered.end(),
                       std::make_move_iterator(list.begin()),
                       std::make_move_iterator(list.end()));
    }

    return ordered;
}

}  // namespace MirrorsUtils

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_MIRRORS_HPP
