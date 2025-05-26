// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/file_system/git_tree_utils.hpp"

#include <cstddef>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/fs_utils.hpp"

namespace {

/// \brief Mark a Git hash as corresponding to a valid tree by creating a
/// corresponding marker file.
/// \returns Success flag.
[[nodiscard]] auto MarkTreeValid(StorageConfig const& storage_config,
                                 std::string const& tree_id) noexcept -> bool {
    auto const marker =
        StorageUtils::GetValidTreesMarkerFile(storage_config, tree_id);
    return FileSystemManager::CreateDirectory(marker.parent_path()) and
           FileSystemManager::CreateFile(marker);
}

/// \brief Checks if a given Git hash is known to correspond to a validated
/// tree by checking the existence of its respective marker file.
/// \returns Existence flag signaling validation.
[[nodiscard]] auto IsTreeValid(StorageConfig const& storage_config,
                               std::string const& tree_hash) noexcept -> bool {
    // check in all generations
    for (std::size_t generation = 0;
         generation < storage_config.num_generations;
         ++generation) {
        if (FileSystemManager::Exists(StorageUtils::GetValidTreesMarkerFile(
                storage_config, tree_hash, generation))) {
            // ensure it is marked in current generation
            return generation == 0 ? true
                                   : MarkTreeValid(storage_config, tree_hash);
        }
    }
    return false;
}

/// \brief Validate a GitTree's subtrees recursively.
/// \returns True if all the subtrees are valid.
[[nodiscard]] auto ValidateGitSubtrees(StorageConfig const& storage_config,
                                       GitTree const& tree) noexcept -> bool {
    for (auto const& [path, entry] : tree) {
        if (IsTreeObject(entry->Type())) {
            auto const hash = entry->Hash();
            if (not IsTreeValid(storage_config, hash)) {
                // validate subtree
                auto subtree = entry->Tree();
                if (not subtree or
                    not ValidateGitSubtrees(storage_config, *subtree) or
                    not MarkTreeValid(storage_config, hash)) {
                    return false;
                }
            }
        }
    }
    return true;
}

}  // namespace

namespace GitTreeUtils {

auto ReadValidGitCASTree(StorageConfig const& storage_config,
                         std::string const& tree_id,
                         GitCASPtr const& git_cas) noexcept
    -> std::optional<GitTree> {
    if (IsTreeValid(storage_config, tree_id)) {
        // read tree without extra checks
        return GitTree::Read(
            git_cas, tree_id, /*ignore_special=*/false, /*skip_checks=*/true);
    }
    // read GitTree from Git with checks and validate its subtrees recursively
    if (auto tree = GitTree::Read(git_cas, tree_id)) {
        if (ValidateGitSubtrees(storage_config, *tree) and
            MarkTreeValid(storage_config, tree_id)) {
            return tree;
        }
    }
    return std::nullopt;
}

auto IsGitTreeValid(StorageConfig const& storage_config,
                    GitTreeEntryPtr const& entry) noexcept -> bool {
    if (entry == nullptr) {
        return false;
    }
    auto tree_id = entry->Hash();
    if (IsTreeValid(storage_config, tree_id)) {
        return true;
    }
    // read underlying GitTree and validate its subtrees recursively
    if (auto const& read_tree = entry->Tree()) {
        if (ValidateGitSubtrees(storage_config, *read_tree) and
            MarkTreeValid(storage_config, tree_id)) {
            return true;
        }
    }
    return false;
}

}  // namespace GitTreeUtils
