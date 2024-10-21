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

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/other_tools/ops_maps/git_tree_fetch_map.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct TreeIdInfo {
    GitTreeInfo tree_info; /* key */
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
    AsyncMapConsumer<TreeIdInfo,
                     std::pair<nlohmann::json /*root*/, bool /*is_cache_hit*/>>;

[[nodiscard]] auto CreateTreeIdGitMap(
    gsl::not_null<GitTreeFetchMap*> const& git_tree_fetch_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    bool fetch_absent,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    Storage const* compat_storage,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    std::size_t jobs) -> TreeIdGitMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_TREE_ID_GIT_MAP_HPP
