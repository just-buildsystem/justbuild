// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_FOREIGN_FILE_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_FOREIGN_FILE_GIT_MAP_HPP

#include <cstddef>
#include <optional>
#include <utility>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"

/// \brief Maps a foreign file to the resulting Git tree WS root,
/// together with the information whether it was a cache hit.
using ForeignFileGitMap =
    AsyncMapConsumer<ForeignFileInfo,
                     std::pair<nlohmann::json /*root*/, bool /*is_cache_hit*/>>;

[[nodiscard]] auto CreateForeignFileGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& storage_config,
    gsl::not_null<Storage const*> const& storage,
    bool fetch_absent,
    std::size_t jobs) -> ForeignFileGitMap;

#endif
