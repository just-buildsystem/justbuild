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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_FS_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_FS_UTILS_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/symlinks/pragma_special.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"

/* Utilities related to CAS and paths therein */

namespace StorageUtils {

/// \brief Get location of Git repository. Defaults to the Git cache root when
/// no better location is found.
[[nodiscard]] auto GetGitRoot(StorageConfig const& storage_config,
                              LocalPathsPtr const& just_mr_paths,
                              std::string const& repo_url) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the tree id associated with
/// a given commit.
[[nodiscard]] auto GetCommitTreeIDFile(StorageConfig const& storage_config,
                                       std::string const& commit,
                                       std::size_t generation = 0) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the tree id of an archive
/// content.
[[nodiscard]] auto GetArchiveTreeIDFile(StorageConfig const& storage_config,
                                        std::string const& repo_type,
                                        std::string const& content,
                                        std::size_t generation = 0) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the tree id of an archive
/// content.
[[nodiscard]] auto GetForeignFileTreeIDFile(StorageConfig const& storage_config,
                                            std::string const& content,
                                            std::string const& name,
                                            bool executable,
                                            std::size_t generation = 0) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the tree id of a distdir list
/// content.
[[nodiscard]] auto GetDistdirTreeIDFile(StorageConfig const& storage_config,
                                        std::string const& content,
                                        std::size_t generation = 0) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing a resolved tree hash.
[[nodiscard]] auto GetResolvedTreeIDFile(StorageConfig const& storage_config,
                                         std::string const& tree_hash,
                                         PragmaSpecial const& pragma_special,
                                         std::size_t generation = 0) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the corresponding artifact hashed by
/// a different hash function.
/// \param storage_config  Storage under which the file is to be found.
/// \param target_hash_type  Hash type to identify mapping target.
/// \param hash  Hash to identify mapping source.
/// \param from_git  Flag to distinguish further mapping source (CAS / GitCAS)
/// \param generation  Further specificity in location of the file.
[[nodiscard]] auto GetRehashIDFile(StorageConfig const& storage_config,
                                   HashFunction::Type target_hash_type,
                                   std::string const& hash,
                                   bool from_git,
                                   std::size_t generation = 0) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file marking a known valid Git tree.
[[nodiscard]] auto GetValidTreesMarkerFile(StorageConfig const& storage_config,
                                           std::string const& tree_hash,
                                           std::size_t generation = 0) noexcept
    -> std::filesystem::path;

/// \brief Write a tree id to file. The parent folder of the file must exist!
[[nodiscard]] auto WriteTreeIDFile(std::filesystem::path const& tree_id_file,
                                   std::string const& tree_id) noexcept -> bool;

/// \brief Add data to file CAS.
/// Returns the path to the file added to CAS, or nullopt if not added.
[[nodiscard]] auto AddToCAS(Storage const& storage,
                            std::string const& data) noexcept
    -> std::optional<std::filesystem::path>;

/// \brief Try to add distfile to CAS.
void AddDistfileToCAS(Storage const& storage,
                      std::filesystem::path const& distfile,
                      LocalPathsPtr const& just_mr_paths) noexcept;

}  // namespace StorageUtils

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_FS_UTILS_HPP
