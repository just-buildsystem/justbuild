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

#ifndef INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_TREE_FETCH_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_TREE_FETCH_MAP_HPP

#include <map>
#include <string>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"

// Stores all the information needed to make a Git tree available
struct GitTreeInfo {
    std::string hash{}; /* key */
    std::map<std::string, std::string> env_vars{};
    std::vector<std::string> inherit_env{};
    std::vector<std::string> command{};
    // name of repository for which work is done; used in progress reporting
    std::string origin{};

    [[nodiscard]] auto operator==(const GitTreeInfo& other) const -> bool {
        return hash == other.hash;
    }
};

namespace std {
template <>
struct hash<GitTreeInfo> {
    [[nodiscard]] auto operator()(const GitTreeInfo& gti) const noexcept
        -> std::size_t {
        return std::hash<std::string>{}(gti.hash);
    }
};
}  // namespace std

/// \brief Maps a known tree provided through a generic command to a flag
/// signaling if there was a cache hit (i.e., tree was already present).
using GitTreeFetchMap = AsyncMapConsumer<GitTreeInfo, bool>;

[[nodiscard]] auto CreateGitTreeFetchMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    bool serve_api_exists,
    IExecutionApi* local_api,
    IExecutionApi* remote_api,
    bool backup_to_remote,
    std::size_t jobs) -> GitTreeFetchMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_TREE_FETCH_MAP_HPP
