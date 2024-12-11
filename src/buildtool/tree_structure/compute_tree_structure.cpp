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

#include "src/buildtool/tree_structure/compute_tree_structure.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/path.hpp"

auto ComputeTreeStructure(ArtifactDigest const& tree,
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
                auto sub_tree =
                    ComputeTreeStructure(*git_digest, storage, cache);
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
