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

#ifndef INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_IMPORT_TO_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_IMPORT_TO_GIT_MAP_HPP

#include <filesystem>
#include <string>
#include <utility>

#include "gsl/gsl"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/utils/cpp/path.hpp"
#include "src/utils/cpp/path_hash.hpp"

struct CommitInfo {
    std::filesystem::path target_path{}; /*key*/
    std::string repo_type{};
    std::string content{};  // hash or path

    CommitInfo(std::filesystem::path const& target_path_,
               std::string repo_type_,
               std::string content_)
        : target_path{std::filesystem::absolute(ToNormalPath(target_path_))},
          repo_type{std::move(repo_type_)},
          content{std::move(content_)} {};

    [[nodiscard]] auto operator==(CommitInfo const& other) const noexcept
        -> bool {
        return target_path == other.target_path;
    }
};

namespace std {
template <>
struct hash<CommitInfo> {
    [[nodiscard]] auto operator()(CommitInfo const& ct) const noexcept
        -> std::size_t {
        return std::hash<std::filesystem::path>{}(ct.target_path);
    }
};
}  // namespace std

/// \brief Maps a directory on the file system to a pair of the tree hash of the
/// content of the directory and the Git ODB it is now a part of.
using ImportToGitMap =
    AsyncMapConsumer<CommitInfo, std::pair<std::string, GitCASPtr>>;

[[nodiscard]] auto CreateImportToGitMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    std::size_t jobs) -> ImportToGitMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_IMPORT_TO_GIT_MAP_HPP