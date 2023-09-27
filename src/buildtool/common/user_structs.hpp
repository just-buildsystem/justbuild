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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_USER_STRUCTS_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_USER_STRUCTS_HPP

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/main/constants.hpp"

/* Structures populated exclusively from command line with user-defined data */

struct LocalPaths {
    // user-defined locations
    std::optional<std::filesystem::path> root{std::nullopt};
    std::filesystem::path setup_root{FileSystemManager::GetCurrentDirectory()};
    std::optional<std::filesystem::path> workspace_root{
        // find workspace root
        []() -> std::optional<std::filesystem::path> {
            std::function<bool(std::filesystem::path const&)>
                is_workspace_root = [&](std::filesystem::path const& path) {
                    return std::any_of(
                        kRootMarkers.begin(),
                        kRootMarkers.end(),
                        [&path](auto const& marker) {
                            return FileSystemManager::Exists(path / marker);
                        });
                };
            // default path to current dir
            auto path = FileSystemManager::GetCurrentDirectory();
            auto root_path = path.root_path();
            while (true) {
                if (is_workspace_root(path)) {
                    return path;
                }
                if (path == root_path) {
                    return std::nullopt;
                }
                path = path.parent_path();
            }
        }()};
    nlohmann::json git_checkout_locations{};
    std::vector<std::filesystem::path> distdirs{};
};

struct CAInfo {
    bool no_ssl_verify{false};
    std::optional<std::filesystem::path> ca_bundle{std::nullopt};
};

using LocalPathsPtr = std::shared_ptr<LocalPaths>;
using CAInfoPtr = std::shared_ptr<CAInfo>;

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_USER_STRUCTS_HPP
