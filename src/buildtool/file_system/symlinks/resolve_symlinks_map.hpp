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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_RESOLVE_SYMLINKS_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_RESOLVE_SYMLINKS_MAP_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>  // std::move

#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/file_system/symlinks/pragma_special.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/path.hpp"
#include "src/utils/cpp/path_hash.hpp"

/// \brief Information needed to resolve an object (blob or tree) given its
/// path relative to the path of a root tree in a given CAS. The unresolved
/// entries should be available in the specified source Git repository, and the
/// resolved entries being made available in the target Git repository.
struct GitObjectToResolve {
    // hash of the root tree
    std::string root_tree_id; /* key */
    // path of this object relative to root tree, in normal form
    std::filesystem::path rel_path{"."}; /* key */
    // how the tree should be resolved
    PragmaSpecial pragma_special{}; /* key */
    // sometimes the info of the object at the required path is already known,
    // so leverage this to avoid extra work
    std::optional<GitRepo::TreeEntryInfo> known_info{std::nullopt};
    // object db to use as source of unresolved entries; it is guaranteed that
    // this repository is treated as read-only if it differs from target_cas
    GitCASPtr source_cas;
    // object db to use as target for resolved entries; can be the same as
    // source_cas and usually it is the Git cache; as the caller has access to
    // such a pointer, it reduces the overhead from opening the Git cache often
    GitCASPtr target_cas;

    GitObjectToResolve() = default;  // needed for cycle detection only!

    GitObjectToResolve(std::string root_tree_id_,
                       std::filesystem::path const& rel_path_,
                       PragmaSpecial const& pragma_special_,
                       std::optional<GitRepo::TreeEntryInfo> known_info_,
                       GitCASPtr source_cas_,
                       GitCASPtr target_cas_)
        : root_tree_id{std::move(root_tree_id_)},
          rel_path{ToNormalPath(rel_path_)},
          pragma_special{pragma_special_},
          known_info{std::move(known_info_)},
          source_cas{std::move(source_cas_)},
          target_cas{std::move(target_cas_)} {};

    [[nodiscard]] auto operator==(
        GitObjectToResolve const& other) const noexcept -> bool {
        return root_tree_id == other.root_tree_id and
               rel_path == other.rel_path and
               pragma_special == other.pragma_special;
    }
};

/// \brief For a possibly initially unresolved path by the end we should be able
/// to know its hash, its type, and its now resolved path.
struct ResolvedGitObject {
    std::string id;
    ObjectType type;
    std::filesystem::path path;
};

/// \brief Maps information about a Git object to its Git ID, type, and path as
/// part of a Git tree where symlinks have been resolved according to the given
/// pragma value.
/// Returns a nullopt only if called on a symlink with pragma ignore special.
/// \note Call the map with type Tree and path "." to resolve a Git tree.
using ResolveSymlinksMap =
    AsyncMapConsumer<GitObjectToResolve, ResolvedGitObject>;

// use explicit cast to std::function to allow template deduction when used
static const std::function<std::string(GitObjectToResolve const&)>
    kGitObjectToResolvePrinter =
        [](GitObjectToResolve const& x) -> std::string { return x.rel_path; };

[[nodiscard]] auto CreateResolveSymlinksMap() -> ResolveSymlinksMap;

namespace std {
template <>
struct hash<GitObjectToResolve> {
    [[nodiscard]] auto operator()(const GitObjectToResolve& ct) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, ct.root_tree_id);
        hash_combine<std::filesystem::path>(&seed, ct.rel_path);
        hash_combine<PragmaSpecial>(&seed, ct.pragma_special);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_RESOLVE_SYMLINKS_MAP_HPP
