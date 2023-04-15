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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_DIRECTORY_TREE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_DIRECTORY_TREE_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"

class DirectoryTree;
using DirectoryTreePtr = gsl::not_null<std::unique_ptr<DirectoryTree>>;

/// \brief Tree of named artifacts. The path through the tree until a leaf node
/// where an artifact is stored represents the file path of that artifact. The
/// tree can be traversed and converted to, e.g., `BlobTree` or
/// `DirectoryNodeBundle`.
class DirectoryTree {
  public:
    using Node = std::variant<DirectoryTreePtr, Artifact const*>;

    /// \brief Add `Artifact*` to tree.
    [[nodiscard]] auto AddArtifact(std::filesystem::path const& path,
                                   Artifact const* artifact) noexcept -> bool;

    /// \brief Create a DirectoryTree from a list of named artifacts.
    [[nodiscard]] static auto FromNamedArtifacts(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const&
            artifacts) noexcept -> std::optional<DirectoryTreePtr>;

    [[nodiscard]] auto begin() const noexcept { return nodes_.begin(); }
    [[nodiscard]] auto end() const noexcept { return nodes_.end(); }
    [[nodiscard]] auto size() const noexcept { return nodes_.size(); }

  private:
    std::unordered_map<std::string, Node> nodes_;

    [[nodiscard]] auto AddArtifact(std::filesystem::path::iterator* begin,
                                   std::filesystem::path::iterator const& end,
                                   Artifact const* artifact) -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_DIRECTORY_TREE_HPP
