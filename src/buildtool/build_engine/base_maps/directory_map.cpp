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

#include "src/buildtool/build_engine/base_maps/directory_map.hpp"

#include <filesystem>
#include <unordered_set>

#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/utils/cpp/path.hpp"

auto BuildMaps::Base::CreateDirectoryEntriesMap(
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs) -> DirectoryEntriesMap {
    auto directory_reader = [repo_config](auto /* unused*/,
                                          auto setter,
                                          auto logger,
                                          auto /* unused */,
                                          auto const& key) {
        auto const* ws_root = repo_config->WorkspaceRoot(key.repository);
        if (ws_root == nullptr) {
            (*logger)(
                fmt::format("Cannot determine workspace root for repository {}",
                            key.repository),
                true);
            return;
        }
        if (ws_root->IsAbsent()) {
            std::string missing_root = "[unknown]";
            auto absent_tree = ws_root->GetAbsentTreeId();
            if (absent_tree) {
                missing_root = *absent_tree;
            }
            (*logger)(fmt::format("Would have to read directory entries of "
                                  "absent root {}.",
                                  missing_root),
                      true);
            return;
        }
        auto dir_path = key.module.empty() ? "." : key.module;
        if (not ws_root->IsDirectory(dir_path)) {
            // Missing directory is fine (source tree might be incomplete),
            // contains no entries.
            (*setter)(FileRoot::DirectoryEntries{
                FileRoot::DirectoryEntries::pairs_t{}});
            return;
        }
        (*setter)(ws_root->ReadDirectory(dir_path));
    };
    return AsyncMapConsumer<BuildMaps::Base::ModuleName,
                            FileRoot::DirectoryEntries>{directory_reader, jobs};
}
