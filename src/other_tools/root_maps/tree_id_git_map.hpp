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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_TREE_ID_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_TREE_ID_GIT_MAP_HPP

#include <string>
#include <utility>
#include <vector>

#include "src/other_tools/ops_maps/git_tree_fetch_map.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct TreeIdInfo {
    GitTreeInfo tree_info{}; /* key */
    // create root that ignores symlinks
    bool ignore_special{}; /* key */
    // create an absent root
    bool absent{}; /* key */

    [[nodiscard]] auto operator==(const TreeIdInfo& other) const -> bool {
        return tree_info == other.tree_info and
               ignore_special == other.ignore_special and
               absent == other.absent;
    }
};

namespace std {
template <>
struct hash<TreeIdInfo> {
    [[nodiscard]] auto operator()(const TreeIdInfo& ti) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<GitTreeInfo>(&seed, ti.tree_info);
        hash_combine<bool>(&seed, ti.ignore_special);
        hash_combine<bool>(&seed, ti.absent);
        return seed;
    }
};
}  // namespace std

/// \brief Maps a known tree provided through a generic command to its
/// workspace root and the information whether it was a cache hit.
using TreeIdGitMap =
    AsyncMapConsumer<TreeIdInfo, std::pair<nlohmann::json, bool>>;

[[nodiscard]] auto CreateTreeIdGitMap(
    gsl::not_null<GitTreeFetchMap*> const& git_tree_fetch_map,
    std::size_t jobs) -> TreeIdGitMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_TREE_ID_GIT_MAP_HPP
