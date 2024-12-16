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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_COMMIT_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_COMMIT_GIT_MAP_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/other_tools/just_mr/mirrors.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct GitRepoInfo {
    // hash can be a commit or tree
    std::string hash; /* key */
    std::string repo_url;
    std::string branch;
    std::string subdir; /* key */
    std::vector<std::string> inherit_env;
    std::vector<std::string> mirrors;
    // name of repository for which work is done; used in progress reporting
    std::string origin;
    // create root that ignores symlinks
    bool ignore_special{}; /* key */
    // create an absent root
    bool absent{}; /* key */

    [[nodiscard]] auto operator==(const GitRepoInfo& other) const -> bool {
        return hash == other.hash and subdir == other.subdir and
               ignore_special == other.ignore_special and
               absent == other.absent;
    }
};

namespace std {
template <>
struct hash<GitRepoInfo> {
    [[nodiscard]] auto operator()(const GitRepoInfo& ct) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, ct.hash);
        hash_combine<std::string>(&seed, ct.subdir);
        hash_combine<bool>(&seed, ct.ignore_special);
        hash_combine<bool>(&seed, ct.absent);
        return seed;
    }
};
}  // namespace std

/// \brief Maps a Git repository commit hash to its tree workspace root,
/// together with the information whether it was a cache hit.
using CommitGitMap =
    AsyncMapConsumer<GitRepoInfo,
                     std::pair<nlohmann::json /*root*/, bool /*is_cache_hit*/>>;

[[nodiscard]] auto CreateCommitGitMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    LocalPathsPtr const& just_mr_paths,
    MirrorsPtr const& additional_mirrors,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    bool fetch_absent,
    gsl::not_null<JustMRProgress*> const& progress,
    std::size_t jobs) -> CommitGitMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_COMMIT_GIT_MAP_HPP
