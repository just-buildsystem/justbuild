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

#include <string>

#include "src/buildtool/common/artifact_digest.hpp"
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
};

#endif  // INCLUDED_SRC_BUILDTOOL_TREE_STRUCTURE_TREE_STRUCTURE_UTILS_HPP
