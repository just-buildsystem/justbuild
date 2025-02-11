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

#include "src/buildtool/main/add_to_cas.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL

#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/file_system/symlinks/resolve_special.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/path.hpp"
#include "src/utils/cpp/path_hash.hpp"

namespace {
/// \brief Class handling import of a filesystem directory to CAS. Allows
/// various treatments of special entries (e.g., symlinks).
class CASTreeImporter final {
  public:
    using FileStoreFunc = std::function<
        std::optional<ArtifactDigest>(std::filesystem::path const&, bool)>;
    using TreeStoreFunc =
        std::function<std::optional<ArtifactDigest>(std::string const&)>;
    using SymlinkStoreFunc =
        std::function<std::optional<ArtifactDigest>(std::string const&)>;

    using KnownPathsMapType =
        std::unordered_map<std::filesystem::path,
                           std::pair<ArtifactDigest, ObjectType>>;

    explicit CASTreeImporter(
        std::filesystem::path const& root,
        FileStoreFunc store_file,
        TreeStoreFunc store_tree,
        SymlinkStoreFunc store_symlink,
        std::optional<ResolveSpecial> resolve_special = std::nullopt) noexcept
        : root_{ToNormalPath(std::filesystem::absolute(root))},
          store_file_{std::move(store_file)},
          store_tree_{std::move(store_tree)},
          store_symlink_{std::move(store_symlink)},
          resolve_special_{resolve_special} {};

    /// \brief Get the Git-tree Digest of the directory, relative to the root.
    [[nodiscard]] auto GetDigest(
        std::filesystem::path const& relative_path = ".") noexcept
        -> std::optional<ArtifactDigest> {
        // cache already computed paths, to avoid extra work
        KnownPathsMapType known_paths;
        // to store directory paths pointed to by symlinks; this allows
        // detection of cycles when upward symlinks are involved
        std::unordered_set<std::filesystem::path> linked_trees;
        return this->CreateGitTreeDigest(
            relative_path, &known_paths, &linked_trees);
    }

  private:
    std::filesystem::path const root_;
    FileStoreFunc const store_file_;
    TreeStoreFunc const store_tree_;
    SymlinkStoreFunc const store_symlink_;
    std::optional<ResolveSpecial> const resolve_special_;

    [[nodiscard]] auto CreateGitTreeDigest(
        std::filesystem::path const& relative_path,
        gsl::not_null<KnownPathsMapType*> const& known_paths,
        gsl::not_null<std::unordered_set<std::filesystem::path>*> const&
            linked_trees) noexcept -> std::optional<ArtifactDigest> {
        try {
            // normalize path
            auto const dir =
                ToNormalPath(std::filesystem::absolute(root_ / relative_path));
            // check the path is pointing to a directory
            if (not FileSystemManager::IsDirectory(dir)) {
                Logger::Log(LogLevel::Error,
                            "Failed to store tree {} -- not a directory",
                            dir.string());
                return std::nullopt;
            }
            // check for cycles in resolving upwards symlinks
            if (not linked_trees->emplace(relative_path).second) {
                Logger::Log(LogLevel::Error,
                            "Failed storing tree {} -- cycle found",
                            dir.string());
                return std::nullopt;
            }
            auto const remove_tree_from_set =
                gsl::finally([&linked_trees, &relative_path]() {
                    linked_trees->erase(relative_path);
                });

            // set up the directory reader lambda
            GitRepo::tree_entries_t entries{};
            auto dir_reader =
                [this, &entries, &relative_path, &known_paths, &linked_trees](
                    std::filesystem::path const& name,
                    ObjectType type) -> bool {
                auto rel_path_to_process = ToNormalPath(relative_path / name);
                auto full_path_to_process =
                    ToNormalPath(root_ / rel_path_to_process);

                // check if entry is cached
                if (auto it = known_paths->find(rel_path_to_process);
                    it != known_paths->end()) {
                    if (auto raw_id = FromHexString(it->second.first.hash())) {
                        entries[std::move(*raw_id)].emplace_back(
                            name.string(), it->second.second);
                        return true;
                    }
                    Logger::Log(LogLevel::Error,
                                "Failed storing entry {}",
                                full_path_to_process.string());
                    return false;
                }

                // original relative path to allow resolvable symlinks to also
                // be cached together with the entries they resolve to
                auto const rel_orig_path = rel_path_to_process;
                std::optional<ObjectType> type_to_process = type;

                // process if symlink; do this first, as symlinks can resolve to
                // other types
                if (IsSymlinkObject(*type_to_process)) {
                    // check if the symlink should be kept as-is
                    if (not resolve_special_ or
                        *resolve_special_ == ResolveSpecial::TreeUpwards) {
                        // read symlink
                        auto const content = FileSystemManager::ReadSymlink(
                            full_path_to_process);
                        if (not content) {
                            Logger::Log(LogLevel::Error,
                                        "Failed reading symlink {}",
                                        full_path_to_process.string());
                            return false;
                        }
                        // if non-upwards, store symlink, cache path and set
                        // entry
                        if (PathIsNonUpwards(*content)) {
                            if (auto digest = store_symlink_(*content)) {
                                known_paths->emplace(
                                    rel_path_to_process,
                                    std::make_pair(*digest,
                                                   ObjectType::Symlink));
                                if (auto raw_id =
                                        FromHexString(digest->hash())) {
                                    entries[std::move(*raw_id)].emplace_back(
                                        name.string(), ObjectType::Symlink);
                                    return true;
                                }
                            }
                            Logger::Log(LogLevel::Error,
                                        "Failed storing symlink {}",
                                        full_path_to_process.string());
                            return false;
                        }
                        // fail for non-upward symlink if on default behaviour
                        if (not resolve_special_) {
                            Logger::Log(
                                LogLevel::Error,
                                "Failed storing symlink {} -- not non-upwards",
                                full_path_to_process.string());
                            return false;
                        }
                    }
                    // resolve the symlink; do so in a loop in order to check,
                    // depending on the resolve strategy, whether the resolve
                    // chain ever goes outside the root tree; the resulting
                    // entry can then be processed as its type
                    std::unordered_set<std::filesystem::path> visited(
                        {rel_path_to_process});
                    while (*type_to_process == ObjectType::Symlink) {
                        // read symlink
                        auto const content = FileSystemManager::ReadSymlink(
                            full_path_to_process);
                        if (not content) {
                            Logger::Log(LogLevel::Error,
                                        "Failed reading symlink {}",
                                        full_path_to_process.string());
                            return false;
                        }
                        if ((*resolve_special_ == ResolveSpecial::TreeUpwards or
                             *resolve_special_ == ResolveSpecial::TreeAll) and
                            not PathIsConfined(*content, rel_path_to_process)) {
                            Logger::Log(LogLevel::Error,
                                        "Failed resolving symlink {} -- not "
                                        "resolving inside root tree",
                                        full_path_to_process.string());
                            return false;
                        }
                        // follow the symlink
                        full_path_to_process = ToNormalPath(
                            full_path_to_process.parent_path() / *content);
                        rel_path_to_process =
                            full_path_to_process.lexically_relative(root_);
                        type_to_process =
                            FileSystemManager::Type(full_path_to_process,
                                                    /*allow_upwards=*/true);
                        if (not type_to_process) {
                            Logger::Log(LogLevel::Error,
                                        "Failed getting type of entry {}",
                                        full_path_to_process.string());
                            return false;
                        }
                        // check for cycle while resolving
                        if (not visited.emplace(rel_path_to_process).second) {
                            Logger::Log(
                                LogLevel::Error,
                                "Failed resolving symlink {} -- cycle found",
                                full_path_to_process.string());
                            return false;
                        }
                    }
                }

                // process if tree; can be initial entry or resolved symlink
                // under that name
                if (IsTreeObject(*type_to_process)) {
                    // store tree and get digest
                    if (auto digest = this->CreateGitTreeDigest(
                            rel_path_to_process, known_paths, linked_trees)) {
                        // cache the path to process, as well as the original
                        // path (in case it is of a symlink)
                        known_paths->emplace(
                            rel_path_to_process,
                            std::make_pair(*digest, ObjectType::Tree));
                        known_paths->emplace(
                            rel_orig_path,
                            std::make_pair(*digest, ObjectType::Tree));
                        // set entry
                        if (auto raw_id = FromHexString(digest->hash())) {
                            entries[std::move(*raw_id)].emplace_back(
                                name.string(), ObjectType::Tree);
                            return true;
                        }
                    }
                    Logger::Log(LogLevel::Error,
                                "Failed storing tree {}",
                                full_path_to_process.string());
                    return false;
                }

                // process if file; can be initial entry or resolved symlink
                // under that name
                if (IsFileObject(*type_to_process)) {
                    // store file and get digest
                    if (auto digest =
                            store_file_(full_path_to_process,
                                        IsExecutableObject(*type_to_process))) {
                        // cache the path to process, as well as the original
                        // path (in case it is of a symlink)
                        known_paths->emplace(
                            rel_path_to_process,
                            std::make_pair(*digest, *type_to_process));
                        known_paths->emplace(
                            rel_orig_path,
                            std::make_pair(*digest, *type_to_process));
                        // set entry
                        if (auto raw_id = FromHexString(digest->hash())) {
                            entries[std::move(*raw_id)].emplace_back(
                                name.string(), *type_to_process);
                            return true;
                        }
                    }
                    Logger::Log(LogLevel::Error,
                                "Failed storing file {}",
                                full_path_to_process.string());
                    return false;
                }

                Logger::Log(LogLevel::Error,
                            "Failed storing entry {} -- unsupported type",
                            full_path_to_process.string());
                return false;
            };

            // read directory entries; skip any special entries if so configured
            if (FileSystemManager::ReadDirectory(
                    dir,
                    dir_reader,
                    /*allow_upwards=*/true,
                    /*ignore_special=*/resolve_special_ ==
                        ResolveSpecial::Ignore)) {
                if (auto tree = GitRepo::CreateShallowTree(entries)) {
                    // store tree
                    return store_tree_(tree->second);
                }
            }
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error, "Storing tree failed with:\n{}", ex.what());
        }
        return std::nullopt;
    }
};
}  // namespace

auto AddArtifactsToCas(ToAddArguments const& clargs,
                       Storage const& storage,
                       ApiBundle const& apis) -> bool {
    auto object_location = clargs.location;
    if (clargs.follow_symlinks) {
        if (not FileSystemManager::ResolveSymlinks(&object_location)) {
            Logger::Log(LogLevel::Error,
                        "Failed resolving {}",
                        clargs.location.string());
            return false;
        }
    }

    auto object_type =
        FileSystemManager::Type(object_location, /*allow_upwards=*/true);
    if (not object_type) {
        Logger::Log(LogLevel::Error,
                    "Non existent or unsupported file-system entry at {}",
                    object_location.string());
        return false;
    }

    auto const& cas = storage.CAS();
    std::optional<ArtifactDigest> digest{};
    switch (*object_type) {
        case ObjectType::File:
            digest = cas.StoreBlob(object_location, /*is_executable=*/false);
            break;
        case ObjectType::Executable:
            digest = cas.StoreBlob(object_location, /*is_executable=*/true);
            break;
        case ObjectType::Symlink: {
            auto content = FileSystemManager::ReadSymlink(object_location);
            if (not content) {
                Logger::Log(LogLevel::Error,
                            "Failed to read symlink at {}",
                            object_location.string());
                return false;
            }
            digest = cas.StoreBlob(*content, /*is_executable=*/false);
        } break;
        case ObjectType::Tree: {
            if (not ProtocolTraits::IsTreeAllowed(
                    cas.GetHashFunction().GetType())) {
                Logger::Log(LogLevel::Error,
                            "Storing of trees only supported in native mode");
                return false;
            }
            auto store_file =
                [&cas](std::filesystem::path const& path,
                       auto is_exec) -> std::optional<ArtifactDigest> {
                return cas.StoreBlob</*kOwner=*/true>(path, is_exec);
            };
            auto store_tree = [&cas](std::string const& content)
                -> std::optional<ArtifactDigest> {
                return cas.StoreTree(content);
            };
            auto store_symlink = [&cas](std::string const& content)
                -> std::optional<ArtifactDigest> {
                return cas.StoreBlob(content);
            };
            CASTreeImporter tree_importer{
                object_location, store_file, store_tree, store_symlink};
            digest = tree_importer.GetDigest();
        } break;
    }

    if (not digest) {
        Logger::Log(LogLevel::Error,
                    "Failed to store {} in local CAS",
                    clargs.location.string());
        return false;
    }

    std::cout << digest->hash() << std::endl;

    auto const object = std::vector<Artifact::ObjectInfo>{
        Artifact::ObjectInfo{*digest, *object_type, false}};

    if (not apis.local->RetrieveToCas(object, *apis.remote)) {
        Logger::Log(LogLevel::Error,
                    "Failed to upload artifact to remote endpoint");
        return false;
    }

    return true;
}

#endif  // BOOTSTRAP_BUILD_TOOL
