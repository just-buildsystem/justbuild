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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_FPATH_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_FPATH_GIT_MAP_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
#include "src/buildtool/file_system/symlinks_map/resolve_symlinks_map.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/other_tools/just_mr/utils.hpp"
#include "src/other_tools/ops_maps/import_to_git_map.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/path_hash.hpp"

struct FpathInfo {
    std::filesystem::path fpath; /* key */
    // create root based on "special" pragma value
    std::optional<PragmaSpecial> pragma_special{std::nullopt}; /* key */
    // create an absent root
    bool absent{}; /* key */

    [[nodiscard]] auto operator==(const FpathInfo& other) const noexcept
        -> bool {
        return fpath == other.fpath and
               pragma_special == other.pragma_special and
               absent == other.absent;
    }
};

/// \brief Maps the path to a repo on the file system to its Git tree WS root.
using FilePathGitMap = AsyncMapConsumer<FpathInfo, nlohmann::json>;

[[nodiscard]] auto CreateFilePathGitMap(
    std::optional<std::string> const& current_subcmd,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& storage_config,
    IExecutionApi const* remote_api,
    std::size_t jobs,
    std::string multi_repo_tool_name,
    std::string build_tool_name) -> FilePathGitMap;

namespace std {
template <>
struct hash<FpathInfo> {
    [[nodiscard]] auto operator()(FpathInfo const& ct) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<std::filesystem::path>(&seed, ct.fpath);
        hash_combine<std::optional<PragmaSpecial>>(&seed, ct.pragma_special);
        hash_combine<bool>(&seed, ct.absent);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_FPATH_GIT_MAP_HPP
