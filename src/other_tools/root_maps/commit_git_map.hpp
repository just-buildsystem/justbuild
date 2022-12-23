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

#include <string>

#include "nlohmann/json.hpp"
#include "src/other_tools/just_mr/utils.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct GitRepoInfo {
    // hash can be a commit or tree
    std::string hash{}; /* key */
    std::string repo_url{};
    std::string branch{};
    std::string subdir{}; /* key */

    [[nodiscard]] auto operator==(const GitRepoInfo& other) const -> bool {
        return hash == other.hash and subdir == other.subdir;
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
        return seed;
    }
};
}  // namespace std

/// \brief Maps a Git repository commit hash to its tree workspace root.
using CommitGitMap = AsyncMapConsumer<GitRepoInfo, nlohmann::json>;

[[nodiscard]] auto CreateCommitGitMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    JustMR::PathsPtr const& just_mr_paths,
    std::size_t jobs) -> CommitGitMap;

void EnsureCommit(GitRepoInfo const& repo_info,
                  std::string const& fetch_repo,
                  std::filesystem::path const& repo_root,
                  GitCASPtr const& git_cas,
                  gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
                  gsl::not_null<TaskSystem*> const& ts,
                  CommitGitMap::SetterPtr const& ws_setter,
                  CommitGitMap::LoggerPtr const& logger);

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_COMMIT_GIT_MAP_HPP