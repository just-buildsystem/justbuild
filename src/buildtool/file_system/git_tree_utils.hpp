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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_UTILS_HPP

#include <optional>
#include <string>

#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_tree.hpp"
#include "src/buildtool/storage/config.hpp"

/// \brief Utility methods for validating GitTree instances
namespace GitTreeUtils {

/// \brief Read a GitTree from a Git repository and ensure (recursively) that it
/// is free of upwards symlinks. Performs storage-based caching of all found
/// valid tree hashes.
/// \param storage_config   Storage instance for caching valid tree hashes.
/// \param tree_id  Git identifier of the tree to read and validate.
/// \param git_cas  Git repository providing the tree.
/// \returns GitTree instance free of upwards symlinks, recursively, on success
/// or nullopt on failure.
[[nodiscard]] auto ReadValidGitCASTree(StorageConfig const& storage_config,
                                       std::string const& tree_id,
                                       GitCASPtr const& git_cas) noexcept
    -> std::optional<GitTree>;

/// \brief Validate a known GitTreeEntry pointing to a Git tree, by checking
/// recursively that it is free of upwards symlinks. Performs storage-based
/// caching of all found valid tree hashes.
/// \param storage_config   Storage instance for caching valid tree hashes.
/// \param GitTreeEntryPtr  Pointer to an existing GitTreeEntry.
/// \returns Flag stating if tree is (recursively) free of upwards symlinks.
/// \note This method is useful when one has fast (and preferably cached) access
/// to a GitTree instance and direct reading from a repository is not desired.
[[nodiscard]] auto IsGitTreeValid(StorageConfig const& storage_config,
                                  GitTreeEntryPtr const& entry) noexcept
    -> bool;

}  // namespace GitTreeUtils

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_TREE_UTILS_HPP
