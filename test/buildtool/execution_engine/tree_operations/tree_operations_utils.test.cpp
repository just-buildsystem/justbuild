// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you
// may not use this file except in compliance with the License. You may
// obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.

#include "src/buildtool/execution_engine/tree_operations/tree_operations_utils.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

using TreeEntries = TreeOperationsUtils::TreeEntries;
using TreeEntry = TreeOperationsUtils::TreeEntry;

// Creates a chain of nested two-entry trees of n levels with both
// entries at each tree level pointing to the next single subtree. The
// tree at the last level points the blobs with the given contents.
//
// tree_1 --t1--> tree_2 --t1--> tree_3 -- ... --> tree_n --b1--> blob_1
//      \---t2----^    \---t2----^    \--- ... ----^    \---b2--> blob_2
//                                                      \--- ...
[[nodiscard]] static auto CreateNestedTree(
    int levels,
    IExecutionApi const& api,
    HashFunction const& hash_function,
    std::unordered_map<std::string, std::string> const& blobs) noexcept
    -> std::optional<Artifact::ObjectInfo> {
    if (levels > 1) {
        // Create subtree with number of levels - 1.
        auto tree_info =
            CreateNestedTree(levels - 1, api, hash_function, blobs);
        if (not tree_info) {
            return std::nullopt;
        }

        // Create tree with two entries pointing to the subtree.
        TreeEntries entries{};
        entries["tree1"] = TreeEntry{.info = *tree_info};
        entries["tree2"] = TreeEntry{.info = *tree_info};
        auto tree = TreeOperationsUtils::WriteTree(api, entries);
        if (not tree) {
            return std::nullopt;
        }
        return *tree;
    }

    // Create blob entries.
    TreeEntries entries{};
    for (auto const& [blob_name, blob_content] : blobs) {
        auto blob_info = Artifact::ObjectInfo{
            .digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                hash_function, blob_content),
            .type = ObjectType::File};
        entries[blob_name] = TreeEntry{.info = std::move(blob_info)};
    }

    // Create tree containing the blobs.
    auto tree = TreeOperationsUtils::WriteTree(api, entries);
    if (not tree) {
        return std::nullopt;
    }
    return *tree;
}

TEST_CASE("TreeOperationsUtils", "[tree_operations]") {
    // Create local execution api.
    auto local_exec_config = CreateLocalExecConfig();
    auto storage_config = TestStorageConfig::Create();
    auto storage = Storage::Create(&storage_config.Get());
    LocalContext local_context{.exec_config = &local_exec_config,
                               .storage_config = &storage_config.Get(),
                               .storage = &storage};
    IExecutionApi::Ptr local_api{new LocalApi{&local_context}};
    HashFunction hash_function{local_api->GetHashType()};

    SECTION("No duplicated tree-overlay calculations") {
        // Create two long nested trees.
        constexpr int kTreeLevels = 65;
        auto base_tree_info = CreateNestedTree(
            kTreeLevels, *local_api, hash_function, {{"foo", "foo"}});
        REQUIRE(base_tree_info);
        auto other_tree_info = CreateNestedTree(
            kTreeLevels, *local_api, hash_function, {{"bar", "bar"}});
        REQUIRE(other_tree_info);

        // Compute tree overlay. A naive tree-overlay computation of
        // these trees has a time complexity of O(2^n). A properly
        // deduplicated tree-overlay computation has only O(n) and will
        // finish in a reasonable amount of time.
        auto tree_overlay =
            TreeOperationsUtils::ComputeTreeOverlay(*local_api,
                                                    *base_tree_info,
                                                    *other_tree_info,
                                                    /*disjoint=*/false);
        REQUIRE(tree_overlay);

        // Check actual result.
        auto result_tree_info =
            CreateNestedTree(kTreeLevels,
                             *local_api,
                             hash_function,
                             {{"foo", "foo"}, {"bar", "bar"}});
        REQUIRE(result_tree_info);
        CHECK(*tree_overlay == *result_tree_info);
    }
}
