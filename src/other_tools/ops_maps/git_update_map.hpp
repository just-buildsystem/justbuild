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

#ifndef INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_UPDATE_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_UPDATE_MAP_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/other_tools/just_mr/mirrors.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct RepoDescriptionForUpdating {
    std::string repo;
    std::string branch;
    std::vector<std::string> inherit_env; /*non-key!*/

    [[nodiscard]] auto operator==(const RepoDescriptionForUpdating& other) const
        -> bool {
        return repo == other.repo and branch == other.branch;
    }
};

/// \brief Maps a pair of repository url and branch to an updated commit hash.
using GitUpdateMap = AsyncMapConsumer<RepoDescriptionForUpdating, std::string>;

namespace std {
template <>
struct hash<RepoDescriptionForUpdating> {
    [[nodiscard]] auto operator()(
        RepoDescriptionForUpdating const& ct) const noexcept -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, ct.repo);
        hash_combine<std::string>(&seed, ct.branch);
        return seed;
    }
};
}  // namespace std

[[nodiscard]] auto CreateGitUpdateMap(
    GitCASPtr const& git_cas,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    MirrorsPtr const& mirrors,
    gsl::not_null<StorageConfig const*> const&
        storage_config,  // native storage config
    gsl::not_null<JustMRStatistics*> const& stats,
    gsl::not_null<JustMRProgress*> const& progress,
    std::size_t jobs) -> GitUpdateMap;

// use explicit cast to std::function to allow template deduction when used
static const std::function<std::string(RepoDescriptionForUpdating const&)>
    kRepoDescriptionPrinter =
        [](RepoDescriptionForUpdating const& x) -> std::string {
    return x.repo;
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_GIT_UPDATE_MAP_HPP
