// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_BLOB_TREE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_BLOB_TREE_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"

class BlobTree;
using BlobTreePtr = gsl::not_null<std::shared_ptr<BlobTree>>;

/// \brief Tree-like blob container to enable tree-invariant satisfying blob
/// upload.
class BlobTree {
  public:
    explicit BlobTree(ArtifactBlob blob, std::vector<BlobTreePtr> nodes)
        : blob_{std::move(blob)}, nodes_{std::move(nodes)} {}

    [[nodiscard]] auto Blob() const noexcept -> ArtifactBlob { return blob_; }
    [[nodiscard]] auto IsTree() const noexcept -> bool {
        return NativeSupport::IsTree(
            static_cast<bazel_re::Digest>(blob_.digest).hash());
    }

    /// \brief Create a `BlobTree` from a `DirectoryTree`.
    [[nodiscard]] static auto FromDirectoryTree(
        DirectoryTreePtr const& tree,
        std::filesystem::path const& parent = "") noexcept
        -> std::optional<BlobTreePtr>;

    [[nodiscard]] auto begin() const noexcept { return nodes_.begin(); }
    [[nodiscard]] auto end() const noexcept { return nodes_.end(); }
    [[nodiscard]] auto size() const noexcept { return nodes_.size(); }

  private:
    ArtifactBlob blob_;
    std::vector<BlobTreePtr> nodes_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_BLOB_TREE_HPP
