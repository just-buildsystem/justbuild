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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_CONTENT_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_CONTENT_GIT_MAP_HPP

#include <cstddef>
#include <optional>
#include <utility>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/file_system/symlinks_map/resolve_symlinks_map.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/just_mr/mirrors.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"

/// \brief Maps the content of an archive to the resulting Git tree WS root,
/// together with the information whether it was a cache hit.
using ContentGitMap =
    AsyncMapConsumer<ArchiveRepoInfo,
                     std::pair<nlohmann::json /*root*/, bool /*is_cache_hit*/>>;

[[nodiscard]] auto CreateContentGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    LocalPathsPtr const& just_mr_paths,
    MirrorsPtr const& additional_mirrors,
    CAInfoPtr const& ca_info,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& storage_config,
    gsl::not_null<Storage const*> const& storage,
    IExecutionApi const* remote_api,
    bool fetch_absent,
    gsl::not_null<JustMRProgress*> const& progress,
    std::size_t jobs) -> ContentGitMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_CONTENT_GIT_MAP_HPP
