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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/tree_structure/compute_tree_structure.hpp"
#include "src/buildtool/tree_structure/tree_structure_cache.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/path.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"
#include "test/utils/large_objects/large_object_utils.hpp"

namespace {
[[nodiscard]] auto CreateFlatTestDirectory(StorageConfig const& storage_config,
                                           Storage const& storage,
                                           std::uintmax_t entries)
    -> std::optional<ArtifactDigest>;

[[nodiscard]] auto CreateComplexTestDirectory(
    StorageConfig const& storage_config,
    Storage const& storage) -> std::optional<ArtifactDigest>;

[[nodiscard]] auto ValidateTreeStructure(ArtifactDigest const& digest,
                                         Storage const& storage) -> bool;

struct TreeEntryHasher {
    [[nodiscard]] auto operator()(
        GitRepo::TreeEntry const& entry) const noexcept -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, entry.name);
        hash_combine<ObjectType>(&seed, entry.type);
        return seed;
    }
};

using TreeEntriesHitContainer =
    std::unordered_map<GitRepo::TreeEntry, int, TreeEntryHasher>;

[[nodiscard]] auto CountTreeEntries(
    ArtifactDigest const& digest,
    Storage const& storage,
    gsl::not_null<TreeEntriesHitContainer*> const& container,
    bool increment) -> bool;
}  // namespace

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

TEST_CASE("compute", "[tree_structure]") {
    auto const storage_config = TestStorageConfig::Create();
    if (not ProtocolTraits::IsNative(
            storage_config.Get().hash_function.GetType())) {
        return;
    }

    auto const storage = Storage::Create(&storage_config.Get());
    TreeStructureCache const ts_cache(&storage_config.Get());

    auto const tree = CreateComplexTestDirectory(storage_config.Get(), storage);
    REQUIRE(tree);

    auto const tree_structure = ComputeTreeStructure(*tree, storage, ts_cache);
    REQUIRE(tree_structure);

    REQUIRE(ValidateTreeStructure(*tree_structure, storage));

    TreeEntriesHitContainer container;
    // Add recursively all TreeEntries of the source tree to the container,
    // incrementing counters:
    REQUIRE(CountTreeEntries(*tree, storage, &container, /*increment=*/true));
    // Add recursively all TreeEntries of the tree structure to the container,
    // decrementing counters:
    REQUIRE(CountTreeEntries(
        *tree_structure, storage, &container, /*increment=*/false));

    // All counters must be equal to 0, meaning all entries have been hit.
    auto const all_hit_equally =
        std::all_of(container.begin(), container.end(), [](auto const& p) {
            return p.second == 0;
        });
    REQUIRE(all_hit_equally);
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

[[nodiscard]] auto CreateComplexTestDirectory(
    StorageConfig const& storage_config,
    Storage const& storage) -> std::optional<ArtifactDigest> {
    auto const test_dir = storage_config.CreateTypedTmpDir("tmp");
    auto head_temp_directory = TmpDir::Create(test_dir->GetPath() / "head_dir");
    auto const head_temp_dir_path = head_temp_directory->GetPath();
    // ├── exec_1
    // ├── file_1
    // ├── symlink_to_nested_dir_1_1 -> nested_dir_1 / nested_dir_1_1
    // ├── symlink_to_nested_dir_2_1 -> nested_dir_2 / nested_dir_2_1
    // ├── nested_dir_1
    // │   ├── ...
    // │   ├── nested_dir_1_1
    // │   │   └── ...
    // │   └── nested_dir_1_2
    // │       └── ...
    // └── nested_dir_2
    //     ├── ...
    //     ├── nested_dir_2_1
    //     │   └── ...
    //     └── nested_dir_2_2
    //         └── ...
    static constexpr std::size_t kFileSize = 128;
    auto const file_path = head_temp_dir_path / "file_1";
    if (not LargeObjectUtils::GenerateFile(file_path, kFileSize)) {
        return std::nullopt;
    }

    auto const exec_path = head_temp_dir_path / "exec_1";
    if (not LargeObjectUtils::GenerateFile(exec_path,
                                           kFileSize,
                                           /*is_executable =*/true)) {
        return std::nullopt;
    }

    std::array const directories = {
        head_temp_dir_path / "nested_dir_1",
        head_temp_dir_path / "nested_dir_1" / "nested_dir_1_1",
        head_temp_dir_path / "nested_dir_1" / "nested_dir_1_2",
        head_temp_dir_path / "nested_dir_2",
        head_temp_dir_path / "nested_dir_2" / "nested_dir_2_1",
        head_temp_dir_path / "nested_dir_2" / "nested_dir_2_2"};

    static constexpr std::size_t kDirEntries = 16;
    for (auto const& path : directories) {
        if (not LargeObjectUtils::GenerateDirectory(path, kDirEntries)) {
            return std::nullopt;
        }
    }

    // Create non-upwards symlinks in the top directory:
    if (not FileSystemManager::CreateNonUpwardsSymlink(
            std::filesystem::path("nested_dir_1") / "nested_dir_1_1",
            head_temp_dir_path / "symlink_to_nested_dir_1_1") or
        not FileSystemManager::CreateNonUpwardsSymlink(
            std::filesystem::path("nested_dir_2") / "nested_dir_2_1",
            head_temp_dir_path / "symlink_to_nested_dir_2_1")) {
        return std::nullopt;
    }
    return CreateDirectory(head_temp_dir_path, storage);
}

[[nodiscard]] auto ReadGitTree(Storage const& storage,
                               ArtifactDigest const& tree)
    -> std::optional<GitRepo::tree_entries_t> {
    auto const tree_path = storage.CAS().TreePath(tree);
    if (not tree_path) {
        return std::nullopt;
    }

    auto const tree_content = FileSystemManager::ReadFile(*tree_path);
    if (not tree_content) {
        return std::nullopt;
    }

    auto const check_symlinks =
        [&storage](std::vector<ArtifactDigest> const& ids) {
            return std::all_of(
                ids.begin(), ids.end(), [&storage](auto const& id) -> bool {
                    auto path_to_symlink =
                        storage.CAS().BlobPath(id, /*is_executable=*/false);
                    if (not path_to_symlink) {
                        return false;
                    }
                    auto const content =
                        FileSystemManager::ReadFile(*path_to_symlink);
                    return content and PathIsNonUpwards(*content);
                });
        };
    return GitRepo::ReadTreeData(*tree_content,
                                 tree.hash(),
                                 check_symlinks,
                                 /*is_hex_id=*/true);
}

[[nodiscard]] auto ValidateTreeStructure(ArtifactDigest const& digest,
                                         Storage const& storage) -> bool {
    auto tree_entries = ReadGitTree(storage, digest);
    if (not tree_entries) {
        return false;
    }

    auto const empty_blob_hash = ArtifactDigest{}.hash();
    for (auto const& [raw_id, es] : *tree_entries) {
        auto const hex_id = ToHexString(raw_id);
        for (auto const& entry : es) {
            switch (entry.type) {
                case ObjectType::Tree: {
                    auto const git_digest = ArtifactDigestFactory::Create(
                        HashFunction::Type::GitSHA1,
                        hex_id,
                        /*size is unknown*/ 0,
                        IsTreeObject(entry.type));
                    if (not git_digest or
                        not ValidateTreeStructure(*git_digest, storage)) {
                        return false;
                    }
                } break;
                default: {
                    if (hex_id != empty_blob_hash) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

[[nodiscard]] auto CountTreeEntries(
    ArtifactDigest const& digest,
    Storage const& storage,
    gsl::not_null<TreeEntriesHitContainer*> const& container,
    bool increment) -> bool {
    auto tree_entries = ReadGitTree(storage, digest);
    if (not tree_entries) {
        return false;
    }

    auto const empty_blob_hash =
        ArtifactDigestFactory::HashDataAs<ObjectType::File>(
            storage.GetHashFunction(), std::string{});
    for (auto const& [raw_id, es] : *tree_entries) {
        auto const hex_id = ToHexString(raw_id);
        for (auto const& entry : es) {
            if (IsTreeObject(entry.type)) {
                auto const git_digest =
                    ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                                  hex_id,
                                                  /*size is unknown*/ 0,
                                                  IsTreeObject(entry.type));
                if (not git_digest or
                    not CountTreeEntries(
                        *git_digest, storage, container, increment)) {
                    return false;
                }
            }
            increment ? ++(*container)[entry] : --(*container)[entry];
        }
    }
    return true;
}

}  // namespace
