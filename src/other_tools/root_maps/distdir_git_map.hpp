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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_DISTDIR_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_DISTDIR_GIT_MAP_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct DistdirInfo {
    std::string content_id; /* key */
    std::shared_ptr<std::unordered_map<std::string, std::string>> content_list;
    std::shared_ptr<std::vector<ArchiveContent>> repos_to_fetch;
    // name of repository for which work is done; used in progress reporting
    std::string origin;
    // create an absent root
    bool absent{}; /* key */

    [[nodiscard]] auto operator==(const DistdirInfo& other) const noexcept
        -> bool {
        return content_id == other.content_id and absent == other.absent;
    }
};

/// \brief Maps a list of repositories belonging to a distdir to its
/// corresponding workspace root and indication whether this was a cache hit.
using DistdirGitMap =
    AsyncMapConsumer<DistdirInfo,
                     std::pair<nlohmann::json /*root*/, bool /*is_cache_hit*/>>;

[[nodiscard]] auto CreateDistdirGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<Storage const*> const& native_storage,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    std::size_t jobs) -> DistdirGitMap;

namespace std {
template <>
struct hash<DistdirInfo> {
    [[nodiscard]] auto operator()(const DistdirInfo& dd) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, dd.content_id);
        hash_combine<bool>(&seed, dd.absent);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_DISTDIR_GIT_MAP_HPP
