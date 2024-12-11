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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/tree_structure/tree_structure_cache.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"
#include "test/utils/large_objects/large_object_utils.hpp"

namespace {
[[nodiscard]] auto CreateFlatTestDirectory(StorageConfig const& storage_config,
                                           Storage const& storage,
                                           std::uintmax_t entries)
    -> std::optional<ArtifactDigest>;
}

TEST_CASE("cache", "[tree_structure]") {
    auto const storage_config = TestStorageConfig::Create();
    if (not ProtocolTraits::IsNative(
            storage_config.Get().hash_function.GetType())) {
        return;
    }

    auto const storage = Storage::Create(&storage_config.Get());
    TreeStructureCache const ts_cache(&storage_config.Get());

    auto const from_dir =
        CreateFlatTestDirectory(storage_config.Get(), storage, 128);
    REQUIRE(from_dir);
    auto const to_dir =
        CreateFlatTestDirectory(storage_config.Get(), storage, 128);
    REQUIRE(to_dir);

    // Set dependency
    REQUIRE(ts_cache.Set(*from_dir, *to_dir));

    // Obtain value
    REQUIRE(ts_cache.Get(*from_dir) == *to_dir);

    // Reseting dependency fails and the entry doesn't get overwritten:
    REQUIRE_FALSE(ts_cache.Set(*from_dir, ArtifactDigest{}));
    REQUIRE(ts_cache.Get(*from_dir) == *to_dir);

    // Rotate generations
    REQUIRE(GarbageCollector::TriggerGarbageCollection(storage_config.Get()));

    auto const youngest = Generation::Create(&storage_config.Get(), 0);
    // Check there's no entry in the youngest generation:
    CHECK_FALSE(youngest.CAS().TreePath(*from_dir).has_value());
    CHECK_FALSE(youngest.CAS().TreePath(*to_dir).has_value());

    // Obtain value one more time and check uplinking has happened:
    REQUIRE(ts_cache.Get(*from_dir) == *to_dir);
    CHECK(youngest.CAS().TreePath(*from_dir).has_value());
    CHECK(youngest.CAS().TreePath(*to_dir).has_value());
}

namespace {
[[nodiscard]] auto CreateDirectory(std::filesystem::path const& directory,
                                   Storage const& storage) {
    BazelMsgFactory::FileStoreFunc store_file =
        [&storage](std::filesystem::path const& path,
                   bool is_exec) -> std::optional<ArtifactDigest> {
        return storage.CAS().StoreBlob(path, is_exec);
    };

    BazelMsgFactory::TreeStoreFunc store_tree =
        [&storage](
            std::string const& content) -> std::optional<ArtifactDigest> {
        return storage.CAS().StoreTree(content);
    };

    BazelMsgFactory::SymlinkStoreFunc store_symlink =
        [&storage](
            std::string const& content) -> std::optional<ArtifactDigest> {
        return storage.CAS().StoreBlob(content, /*is_executable=*/false);
    };

    return BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
        directory, store_file, store_tree, store_symlink);
}

[[nodiscard]] auto CreateFlatTestDirectory(StorageConfig const& storage_config,
                                           Storage const& storage,
                                           std::uintmax_t entries)
    -> std::optional<ArtifactDigest> {
    auto tree = storage_config.CreateTypedTmpDir("tree");
    if (not tree or
        not LargeObjectUtils::GenerateDirectory(tree->GetPath(), entries)) {
        return std::nullopt;
    }
    return CreateDirectory(tree->GetPath(), storage);
}
}  // namespace
