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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_TREE_OPERATIONS_TREE_OPERATIONS_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_TREE_OPERATIONS_TREE_OPERATIONS_UTILS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/utils/cpp/expected.hpp"

/// \brief Utility functions for tree operations.
class TreeOperationsUtils final {
  public:
    struct TreeEntry {
        Artifact::ObjectInfo info{};
        std::optional<std::string> symlink_target{};
    };

    using TreeEntries = std::unordered_map<std::string, TreeEntry>;

    /// \brief Computes a new tree from two existing ones by overlaying
    /// their contents.
    /// \param api              The execution API to be used.
    /// \param base_tree_info   The base tree to be overlayed with another tree.
    /// \param other_tree_info  The other tree to be overlayed with the
    /// base tree.
    /// \param disjoint         If true, abort the computation if a
    /// conflict is encountered, otherwise the conflict is ignored and
    /// the entry from the second tree wins.
    /// \returns The computed overlayed tree or an error message in case
    /// of a conflict, when disjoint mode is used.
    [[nodiscard]] static auto ComputeTreeOverlay(
        IExecutionApi const& api,
        Artifact::ObjectInfo const& base_tree_info,
        Artifact::ObjectInfo const& other_tree_info,
        bool disjoint,
        std::filesystem::path const& where = std::filesystem::path{}) noexcept
        -> expected<Artifact::ObjectInfo, std::string>;

    [[nodiscard]] static auto ReadTree(
        IExecutionApi const& api,
        Artifact::ObjectInfo const& tree_info) noexcept
        -> expected<TreeEntries, std::string>;

    [[nodiscard]] static auto WriteTree(
        IExecutionApi const& api,
        TreeEntries const& tree_entries) noexcept
        -> expected<Artifact::ObjectInfo, std::string>;

  private:
    [[nodiscard]] static auto ParseBazelDirectory(
        std::string const& tree_data,
        HashFunction::Type hash_type) noexcept -> std::optional<TreeEntries>;

    [[nodiscard]] static auto ParseGitTree(
        std::string const& tree_data,
        ArtifactDigest const& tree_digest,
        HashFunction::Type hash_type) noexcept -> std::optional<TreeEntries>;

    [[nodiscard]] static auto SerializeBazelDirectory(
        TreeEntries const& tree_entries) noexcept -> std::optional<std::string>;

    [[nodiscard]] static auto SerializeGitTree(
        TreeEntries const& tree_entries) noexcept -> std::optional<std::string>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_TREE_OPERATIONS_TREE_OPERATIONS_UTILS_HPP
