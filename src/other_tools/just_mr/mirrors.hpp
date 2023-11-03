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

#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/logging/logger.hpp"

struct Mirrors {
    nlohmann::json local_mirrors{};        // maps URLs to list of local mirrors
    nlohmann::json preferred_hostnames{};  // list of mirror hostnames
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

}  // namespace MirrorsUtils

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_MIRRORS_HPP
