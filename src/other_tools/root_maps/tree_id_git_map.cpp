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

#include "src/other_tools/root_maps/tree_id_git_map.hpp"

#include "fmt/core.h"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/storage/config.hpp"

auto CreateTreeIdGitMap(
    gsl::not_null<GitTreeFetchMap*> const& git_tree_fetch_map,
    bool fetch_absent,
    std::size_t jobs) -> TreeIdGitMap {
    auto tree_to_git = [git_tree_fetch_map, fetch_absent](auto ts,
                                                          auto setter,
                                                          auto logger,
                                                          auto /*unused*/,
                                                          auto const& key) {
        // if root is actually absent, no work needs to be done
        if (key.absent and not fetch_absent) {
            auto root = nlohmann::json::array(
                {key.ignore_special ? FileRoot::kGitTreeIgnoreSpecialMarker
                                    : FileRoot::kGitTreeMarker,
                 key.tree_info.hash});
            (*setter)(std::pair(std::move(root), false));
            return;
        }
        // otherwise, one must fetch;
        // make sure the required tree is in Git cache
        git_tree_fetch_map->ConsumeAfterKeysReady(
            ts,
            {key.tree_info},
            [key, setter](auto const& values) {
                // tree is now in Git cache;
                // get cache hit info
                auto is_cache_hit = *values[0];
                // set the workspace root as present
                (*setter)(
                    std::pair(nlohmann::json::array(
                                  {key.ignore_special
                                       ? FileRoot::kGitTreeIgnoreSpecialMarker
                                       : FileRoot::kGitTreeMarker,
                                   key.tree_info.hash,
                                   StorageConfig::GitRoot().string()}),
                              is_cache_hit));
            },
            [logger, tree_id = key.tree_info.hash](auto const& msg,
                                                   bool fatal) {
                (*logger)(fmt::format(
                              "While ensuring git-tree {} is in Git cache:\n{}",
                              tree_id,
                              msg),
                          fatal);
            });
    };
    return AsyncMapConsumer<TreeIdInfo, std::pair<nlohmann::json, bool>>(
        tree_to_git, jobs);
}
