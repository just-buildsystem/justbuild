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

#include "src/buildtool/tree_structure/tree_structure_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/git/git_api.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/path.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

auto TreeStructureUtils::Compute(ArtifactDigest const& tree,
                                 Storage const& storage,
                                 TreeStructureCache const& cache) noexcept
    -> expected<ArtifactDigest, std::string> {
    if (not tree.IsTree() or not ProtocolTraits::IsNative(tree.GetHashType())) {
        return unexpected{fmt::format("Not a git tree: {}", tree.hash())};
    }

    if (auto result = cache.Get(tree)) {
        return *std::move(result);
    }

    auto const tree_path = storage.CAS().TreePath(tree);
    if (not tree_path) {
        return unexpected{
            fmt::format("Failed to read from the storage: {}", tree.hash())};
    }

    auto const tree_content = FileSystemManager::ReadFile(*tree_path);
    if (not tree_content) {
        return unexpected{
            fmt::format("Failed to read content of: {}", tree.hash())};
    }

    auto const check_symlinks =
        [&storage](std::vector<ArtifactDigest> const& ids) {
            return std::all_of(
                ids.begin(), ids.end(), [&storage](auto const& id) -> bool {
                    auto path_to_symlink =
                        storage.CAS().BlobPath(id, /*is_executable=*/false);
                    if (not path_to_symlink) {
                        return false;
                    }
                    auto const content =
                        FileSystemManager::ReadFile(*path_to_symlink);
                    return content and PathIsNonUpwards(*content);
                });
        };
    auto const entries = GitRepo::ReadTreeData(
        *tree_content, tree.hash(), check_symlinks, /*is_hex_id=*/true);
    if (not entries) {
        return unexpected{
            fmt::format("Failed to parse git tree: {}", tree.hash())};
    }

    GitRepo::tree_entries_t structure_entries{};
    for (auto const& [raw_id, es] : *entries) {
        for (auto const& entry : es) {
            std::optional<ArtifactDigest> structure_digest;
            if (IsTreeObject(entry.type)) {
                auto const git_digest =
                    ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                                  ToHexString(raw_id),
                                                  /*size is unknown*/ 0,
                                                  /*is_tree=*/true);
                if (not git_digest) {
                    return unexpected{git_digest.error()};
                }
                auto sub_tree = Compute(*git_digest, storage, cache);
                if (not sub_tree) {
                    return sub_tree;
                }
                structure_digest = *std::move(sub_tree);
            }
            else {
                structure_digest = storage.CAS().StoreBlob(
                    std::string{}, IsExecutableObject(entry.type));
            }
            if (not structure_digest) {
                return unexpected{fmt::format(
                    "Failed to get structure digest for: {}", raw_id)};
            }
            if (auto id = FromHexString(structure_digest->hash())) {
                structure_entries[*std::move(id)].emplace_back(entry);
            }
            else {
                return unexpected{
                    fmt::format("Failed to get raw id for {}", raw_id)};
            }
        }
    }

    auto const structure_tree = GitRepo::CreateShallowTree(structure_entries);
    if (not structure_tree) {
        return unexpected{fmt::format(
            "Failed to create structured Git tree for {}", tree.hash())};
    }

    auto tree_structure = storage.CAS().StoreTree(structure_tree->second);
    if (not tree_structure) {
        return unexpected{fmt::format(
            "Failed to add tree structure to the CAS for {}", tree.hash())};
    }

    if (not cache.Set(tree, *tree_structure)) {
        return unexpected{fmt::format(
            "Failed to create a tree structure cache entry for\n{} => {}",
            tree.hash(),
            tree_structure->hash())};
    }
    return *std::move(tree_structure);
}

auto TreeStructureUtils::ImportToGit(
    ArtifactDigest const& tree,
    IExecutionApi const& source_api,
    StorageConfig const& target_config,
    gsl::not_null<std::mutex*> const& tagging_lock) noexcept
    -> expected<ArtifactDigest, std::string> {
    if (not tree.IsTree() or not ProtocolTraits::IsNative(tree.GetHashType())) {
        return unexpected{fmt::format("Not a git tree: {}", tree.hash())};
    }

    // Check the source contains the tree:
    if (not source_api.IsAvailable(tree)) {
        return unexpected{
            fmt::format("Source doesn't contain tree {}", tree.hash())};
    }

    // Check the tree is in the repository already:
    if (auto in_repo =
            GitRepo::IsTreeInRepo(target_config.GitRoot(), tree.hash())) {
        if (*in_repo) {
            return tree;
        }
    }
    else {
        return unexpected{std::move(in_repo).error()};
    }

    auto const tmp_dir =
        target_config.CreateTypedTmpDir("import_from_cas_to_git");
    if (tmp_dir == nullptr) {
        return unexpected{fmt::format(
            "Failed to create temporary directory for {}", tree.hash())};
    }

    // Stage the tree to a temporary directory:
    if (not source_api.RetrieveToPaths(
            {Artifact::ObjectInfo{tree, ObjectType::Tree}},
            {tmp_dir->GetPath()})) {
        return unexpected{fmt::format(
            "Failed to stage {} to a temporary location.", tree.hash())};
    }

    // Import the result to git:
    auto tree_hash = GitRepo::ImportToGit(
        target_config,
        tmp_dir->GetPath(),
        /*commit_message=*/fmt::format("Import {}", tree.hash()),
        tagging_lock);
    if (not tree_hash) {
        return unexpected{std::move(tree_hash).error()};
    }
    auto digest = ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                                tree_hash.value(),
                                                /*size_unknown=*/0,
                                                /*is_tree=*/true);
    if (not digest) {
        return unexpected{std::move(digest).error()};
    }
    return *digest;
}

auto TreeStructureUtils::ExportFromGit(
    ArtifactDigest const& tree,
    std::vector<std::filesystem::path> const& source_repos,
    StorageConfig const& storage_config,
    IExecutionApi const& target_api) noexcept -> expected<bool, std::string> {
    if (not tree.IsTree() or not ProtocolTraits::IsNative(tree.GetHashType())) {
        return unexpected{fmt::format("Not a git tree: {}", tree.hash())};
    }

    // Find git repo that contains the tree:
    std::filesystem::path const* repo = nullptr;
    for (std::size_t i = 0; i < source_repos.size() and repo == nullptr; ++i) {
        auto in_repo = GitRepo::IsTreeInRepo(source_repos[i], tree.hash());
        if (not in_repo.has_value()) {
            return unexpected{std::move(in_repo).error()};
        }
        if (*in_repo) {
            repo = &source_repos[i];
        }
    }

    // Check that at least one repo contains the tree:
    if (repo == nullptr) {
        return false;
    }

    RepositoryConfig repo_config{};
    if (not repo_config.SetGitCAS(*repo, &storage_config)) {
        return unexpected{
            fmt::format("Failed to set git cas at {}", repo->string())};
    }
    auto const git_api = GitApi{&repo_config};
    return git_api.RetrieveToCas({Artifact::ObjectInfo{tree, ObjectType::Tree}},
                                 target_api);
}

auto TreeStructureUtils::ComputeStructureLocally(
    ArtifactDigest const& tree,
    std::vector<std::filesystem::path> const& known_repositories,
    StorageConfig const& storage_config,
    gsl::not_null<std::mutex*> const& tagging_lock)
    -> expected<std::optional<ArtifactDigest>, std::string> {
    if (not ProtocolTraits::IsNative(tree.GetHashType()) or not tree.IsTree()) {
        return unexpected{fmt::format("Not a git tree: {}", tree.hash())};
    }

    if (not ProtocolTraits::IsNative(storage_config.hash_function.GetType())) {
        return unexpected{fmt::format("Not a native storage config")};
    }

    auto const storage = Storage::Create(&storage_config);
    LocalExecutionConfig const dummy_exec_config{};
    LocalContext const local_context{.exec_config = &dummy_exec_config,
                                     .storage_config = &storage_config,
                                     .storage = &storage};
    LocalApi const local_api{&local_context};

    // First check the result is in cache already:
    TreeStructureCache const tree_structure_cache{&storage_config};
    if (auto const from_cache = tree_structure_cache.Get(tree)) {
        auto to_git =
            ImportToGit(*from_cache, local_api, storage_config, tagging_lock);
        if (not to_git.has_value()) {
            return unexpected{fmt::format("While importing {} to git:\n{}",
                                          from_cache->hash(),
                                          std::move(to_git).error())};
        }
        return std::make_optional<ArtifactDigest>(std::move(to_git).value());
    }

    // If the tree is not in the storage, it must be present in git:
    if (not storage.CAS().TreePath(tree).has_value()) {
        auto in_cas =
            ExportFromGit(tree, known_repositories, storage_config, local_api);
        if (not in_cas.has_value()) {
            return unexpected{
                fmt::format("While exporting {} from git to CAS:\n{}",
                            tree.hash(),
                            std::move(in_cas).error())};
        }

        // If the tree hasn't been found neither in CAS, nor in git, there's
        // nothing else to do:
        if (not storage.CAS().TreePath(tree).has_value()) {
            return std::optional<ArtifactDigest>{};
        }
    }

    // Compute tree structure and add it to the storage and cache:
    auto tree_structure = Compute(tree, storage, tree_structure_cache);
    if (not tree_structure) {
        return unexpected{
            fmt::format("Failed to compute tree structure of {}:\n{}",
                        tree.hash(),
                        std::move(tree_structure).error())};
    }

    // Import the result to git:
    auto to_git =
        ImportToGit(*tree_structure, local_api, storage_config, tagging_lock);
    if (not to_git.has_value()) {
        return unexpected{fmt::format(
            "While importing the resulting tree structure {} to git:\n{}",
            tree_structure->hash(),
            std::move(to_git).error())};
    }
    return std::make_optional<ArtifactDigest>(*std::move(tree_structure));
}
