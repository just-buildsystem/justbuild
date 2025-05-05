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

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/hash_combine.hpp"

namespace std {
template <typename>
struct hash;
}

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
        bool disjoint) noexcept -> expected<Artifact::ObjectInfo, std::string>;

    [[nodiscard]] static auto ReadTree(
        IExecutionApi const& api,
        Artifact::ObjectInfo const& tree_info) noexcept
        -> expected<TreeEntries, std::string>;

    [[nodiscard]] static auto WriteTree(
        IExecutionApi const& api,
        TreeEntries const& tree_entries) noexcept
        -> expected<Artifact::ObjectInfo, std::string>;

  private:
    struct TreePair {
        std::pair<Artifact::ObjectInfo, Artifact::ObjectInfo> trees;
        explicit TreePair(
            std::pair<Artifact::ObjectInfo, Artifact::ObjectInfo> trees)
            : trees{std::move(trees)} {}
        [[nodiscard]] auto operator==(TreePair const& other) const noexcept
            -> bool {
            return trees == other.trees;
        }
    };
    friend struct std::hash<TreePair>;

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

    /// \brief Creates an async map consumer that maps a pair of trees
    /// to their corresponding overlay tree.
    /// \param api              The execution API to be used.
    /// \param disjoint         If true, abort the computation if a
    /// conflict is encountered, otherwise the conflict is ignored and
    /// the entry from the second tree wins.
    /// \returns The async map consumer.
    [[nodiscard]] static auto CreateTreeOverlayMap(IExecutionApi const& api,
                                                   bool disjoint) noexcept
        -> AsyncMapConsumer<TreePair, Artifact::ObjectInfo>;
};

namespace std {
template <>
struct hash<TreeOperationsUtils::TreePair> {
    [[nodiscard]] auto operator()(TreeOperationsUtils::TreePair const& pair)
        const noexcept -> std::size_t {
        std::size_t seed{};
        hash_combine(&seed, pair.trees.first);
        hash_combine(&seed, pair.trees.second);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_TREE_OPERATIONS_TREE_OPERATIONS_UTILS_HPP
