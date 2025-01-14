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

#ifndef INCLUDED_SRC_BUILDTOOL_TREE_STRUCTURE_TREE_STRUCTURE_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_TREE_STRUCTURE_TREE_STRUCTURE_UTILS_HPP

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/tree_structure/tree_structure_cache.hpp"
#include "src/utils/cpp/expected.hpp"

class TreeStructureUtils final {
  public:
    /// \brief Compute the tree structure of a git tree and add corresponding
    /// coupling to the cache. Tree structure is a directory, where all blobs
    /// and symlinks are replaced with empty blobs. Every subtree gets written
    /// to the cache as well. Expects tree is present in the storage.
    /// \param tree         Git tree to be analyzed. Must be present in the
    /// storage.
    /// \param storage      Storage (GitSHA1) to be used for adding new tree
    /// structure artifacts
    /// \param cache        Cache for storing key-value dependencies.
    /// \return Digest of the tree structure that is present in the storage on
    /// success, or an error message on failure.
    [[nodiscard]] static auto Compute(ArtifactDigest const& tree,
                                      Storage const& storage,
                                      TreeStructureCache const& cache) noexcept
        -> expected<ArtifactDigest, std::string>;

    /// \brief Import a git tree from the given source to storage_config's git
    /// repo.
    /// \param tree           GitSHA1 tree to import
    /// \param source_api     The source of the tree. Must be capable of
    /// processing GitSHA1 trees.
    /// \param target_config  Config with target git repo.
    /// \param tagging_lock   Mutex to protect critical tagging operation
    /// \return Digest of the tree that is available in the repo after the
    /// call or an error message on failure.
    [[nodiscard]] static auto ImportToGit(
        ArtifactDigest const& tree,
        IExecutionApi const& source_api,
        StorageConfig const& target_config,
        gsl::not_null<std::mutex*> const& tagging_lock) noexcept
        -> expected<ArtifactDigest, std::string>;

    /// \brief Export a tree from source git repositories to target api. Uses
    /// regular GitApi for retrieval from git and doesn't perform rehashing.
    /// \param tree           Tree to export
    /// \param source_repos   Repositories to check
    /// \param target_api     Api to export the tree to
    /// \return True if target api contains tree after the call, false if none
    /// of source repositories contain tree, or an error message on failure.
    [[nodiscard]] static auto ExportFromGit(
        ArtifactDigest const& tree,
        std::vector<std::filesystem::path> const& source_repos,
        IExecutionApi const& target_api) noexcept
        -> expected<bool, std::string>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_TREE_STRUCTURE_TREE_STRUCTURE_UTILS_HPP
