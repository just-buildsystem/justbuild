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

#include <filesystem>
#include <optional>
#include <string>

#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
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
                                       std::string const& commit) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the tree id of an archive
/// content.
[[nodiscard]] auto GetArchiveTreeIDFile(StorageConfig const& storage_config,
                                        std::string const& repo_type,
                                        std::string const& content) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the tree id of an archive
/// content.
[[nodiscard]] auto GetForeignFileTreeIDFile(StorageConfig const& storage_config,
                                            std::string const& content,
                                            std::string const& name,
                                            bool executable) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing the tree id of a distdir list
/// content.
[[nodiscard]] auto GetDistdirTreeIDFile(StorageConfig const& storage_config,
                                        std::string const& content) noexcept
    -> std::filesystem::path;

/// \brief Get the path to the file storing a resolved tree hash.
[[nodiscard]] auto GetResolvedTreeIDFile(
    StorageConfig const& storage_config,
    std::string const& tree_hash,
    PragmaSpecial const& pragma_special) noexcept -> std::filesystem::path;

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
