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

#ifndef INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_CONTENT_CAS_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_CONTENT_CAS_MAP_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/other_tools/just_mr/mirrors.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct ArchiveContent {
    std::string content{}; /* key */
    std::optional<std::string> distfile{std::nullopt};
    std::string fetch_url{};
    std::vector<std::string> mirrors{};
    std::optional<std::string> sha256{std::nullopt};
    std::optional<std::string> sha512{std::nullopt};
    // name of repository for which work is done; used in progress reporting
    std::string origin{};

    [[nodiscard]] auto operator==(const ArchiveContent& other) const -> bool {
        return content == other.content;
    }
};

// Used in callers of ContentCASMap which need extra fields
struct ArchiveRepoInfo {
    ArchiveContent archive{}; /* key */
    std::string repo_type{};  /* key */
    std::string subdir{};     /* key */
    // create root based on "special" pragma value
    std::optional<PragmaSpecial> pragma_special{std::nullopt}; /* key */
    // create an absent root
    bool absent{}; /* key */

    [[nodiscard]] auto operator==(const ArchiveRepoInfo& other) const -> bool {
        return archive == other.archive and repo_type == other.repo_type and
               subdir == other.subdir and
               pragma_special == other.pragma_special and
               absent == other.absent;
    }
};

struct ForeignFileInfo {
    ArchiveContent archive{}; /* key */
    std::string name{};       /* key */
    bool executable{};        /* key */
    bool absent{};            /* key */

    [[nodiscard]] auto operator==(const ForeignFileInfo& other) const -> bool {
        return archive == other.archive and name == other.name and
               executable == other.executable and absent == other.absent;
    }
};

/// \brief Maps the content hash of an archive to nullptr, as we only care if
/// the map fails or not.
using ContentCASMap = AsyncMapConsumer<ArchiveContent, std::nullptr_t>;

[[nodiscard]] auto CreateContentCASMap(
    LocalPathsPtr const& just_mr_paths,
    MirrorsPtr const& additional_mirrors,
    CAInfoPtr const& ca_info,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    bool serve_api_exists,
    gsl::not_null<IExecutionApi*> const& local_api,
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
    std::size_t jobs) -> ContentCASMap;

namespace std {
template <>
struct hash<ArchiveContent> {
    [[nodiscard]] auto operator()(const ArchiveContent& ct) const noexcept
        -> std::size_t {
        return std::hash<std::string>{}(ct.content);
    }
};

// Used in callers of ContentCASMap which need extra fields
template <>
struct hash<ArchiveRepoInfo> {
    [[nodiscard]] auto operator()(const ArchiveRepoInfo& ct) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<ArchiveContent>(&seed, ct.archive);
        hash_combine<std::string>(&seed, ct.repo_type);
        hash_combine<std::string>(&seed, ct.subdir);
        hash_combine<std::optional<PragmaSpecial>>(&seed, ct.pragma_special);
        hash_combine<bool>(&seed, ct.absent);
        return seed;
    }
};

template <>
struct hash<ForeignFileInfo> {
    [[nodiscard]] auto operator()(const ForeignFileInfo& ct) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<ArchiveContent>(&seed, ct.archive);
        hash_combine<std::string>(&seed, ct.name);
        hash_combine<bool>(&seed, ct.executable);
        hash_combine<bool>(&seed, ct.absent);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_CONTENT_CAS_MAP_HPP
