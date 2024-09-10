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

#include "src/buildtool/storage/large_object_cas.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"
#include "test/utils/large_objects/large_object_utils.hpp"

namespace {
namespace LargeTestUtils {

template <bool IsExecutable>
class Blob final {
  public:
    static constexpr auto kLargeId = std::string_view("bl_8Mb");
    static constexpr auto kLargeSize = std::uintmax_t(8 * 1024 * 1024);

    static constexpr auto kSmallId = std::string_view("bl_1kB");
    static constexpr auto kSmallSize = std::uintmax_t(1024);

    static constexpr auto kEmptyId = std::string_view("bl_0");
    static constexpr auto kEmptySize = std::uintmax_t(0);

    [[nodiscard]] static auto Create(
        LocalCAS<kDefaultDoGlobalUplink> const& cas,
        std::string const& id,
        std::uintmax_t size) noexcept
        -> std::optional<std::pair<ArtifactDigest, std::filesystem::path>>;

    [[nodiscard]] static auto Generate(std::string const& id,
                                       std::uintmax_t size) noexcept
        -> std::optional<std::filesystem::path>;
};

using File = Blob<false>;

class Tree final {
  public:
    static constexpr auto kLargeId = std::string_view("tree_4096");
    static constexpr auto kLargeSize = std::uintmax_t(4096);

    static constexpr auto kSmallId = std::string_view("tree_1");
    static constexpr auto kSmallSize = std::uintmax_t(1);

    static constexpr auto kEmptyId = std::string_view("tree_0");
    static constexpr auto kEmptySize = std::uintmax_t(0);

    [[nodiscard]] static auto Create(
        LocalCAS<kDefaultDoGlobalUplink> const& cas,
        std::string const& id,
        std::uintmax_t entries_count) noexcept
        -> std::optional<std::pair<ArtifactDigest, std::filesystem::path>>;

    [[nodiscard]] static auto Generate(std::string const& id,
                                       std::uintmax_t entries_count) noexcept
        -> std::optional<std::filesystem::path>;

    [[nodiscard]] static auto StoreRaw(
        LocalCAS<kDefaultDoGlobalUplink> const& cas,
        std::filesystem::path const& directory) noexcept
        -> std::optional<ArtifactDigest>;
};

}  // namespace LargeTestUtils
}  // namespace

// Test splitting of a small tree.
TEST_CASE("LargeObjectCAS: split a small tree", "[storage]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const& cas = storage.CAS();

    // Create a small tree:
    using LargeTestUtils::Tree;
    auto small =
        Tree::Create(cas, std::string(Tree::kSmallId), Tree::kSmallSize);
    REQUIRE(small);
    auto const& [digest, path] = *small;

    // Split must be successful:
    auto split_pack = cas.SplitTree(digest);
    REQUIRE(split_pack);

    // The result must contain one blob digest:
    CHECK(split_pack->size() == 1);
    CHECK_FALSE(split_pack->front().IsTree());
}

// Test splitting of a large object. The split must be successful and the entry
// must be placed to the LargeCAS. The second split of the same object must load
// the result from the LargeCAS, no actual split must occur.
// The object can be implicitly reconstructed from the LargeCAS.
template <ObjectType kType>
static void TestLarge(StorageConfig const& storage_config,
                      Storage const& storage) noexcept {
    SECTION("Large") {
        static constexpr bool kIsTree = IsTreeObject(kType);
        static constexpr bool kIsExec = IsExecutableObject(kType);

        using TestType = std::conditional_t<kIsTree,
                                            LargeTestUtils::Tree,
                                            LargeTestUtils::Blob<kIsExec>>;

        auto const& cas = storage.CAS();

        // Create a large object:
        auto object = TestType::Create(
            cas, std::string(TestType::kLargeId), TestType::kLargeSize);
        CHECK(object);
        auto const& [digest, path] = *object;

        // Split the large object:
        auto pack_1 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        CHECK(pack_1);
        CHECK(pack_1->size() > 1);

        CHECK(FileSystemManager::RemoveFile(path));
        CHECK_FALSE(FileSystemManager::IsFile(path));

        SECTION("Split short-circuiting") {
            // Check the second call loads the entry from the large CAS:
            auto pack_2 =
                kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
            CHECK(pack_2);
            CHECK(pack_2->size() == pack_1->size());

            // There must be no spliced file:
            CHECK_FALSE(FileSystemManager::IsFile(path));
        }

        SECTION("Splice") {
            // Check implicit splice:
            auto spliced_path =
                kIsTree ? cas.TreePath(digest) : cas.BlobPath(digest, kIsExec);
            REQUIRE(spliced_path);

            // The result must be in the same location:
            CHECK(*spliced_path == path);
        }

        SECTION("Uplinking") {
            // Increment generation:
            CHECK(GarbageCollector::TriggerGarbageCollection(storage_config));

            // Check implicit splice:
            auto spliced_path =
                kIsTree ? cas.TreePath(digest) : cas.BlobPath(digest, kIsExec);
            REQUIRE(spliced_path);

            // The result must be spliced to the same location:
            CHECK(*spliced_path == path);

            // Check the large entry was uplinked too:
            // Remove the spliced result:
            CHECK(FileSystemManager::RemoveFile(path));
            CHECK_FALSE(FileSystemManager::IsFile(path));

            // Call split with disabled uplinking:
            auto const youngest_storage = ::Generation::Create(&storage_config);
            auto pack_3 = kIsTree ? youngest_storage.CAS().SplitTree(digest)
                                  : youngest_storage.CAS().SplitBlob(digest);
            REQUIRE(pack_3);
            CHECK(pack_3->size() == pack_1->size());

            // Check there are no spliced results in all generations:
            for (std::size_t i = 0; i < storage_config.num_generations; ++i) {
                auto const storage = ::Generation::Create(&storage_config);
                auto generation_path =
                    kIsTree ? storage.CAS().TreePath(digest)
                            : storage.CAS().BlobPath(digest, kIsExec);
                REQUIRE_FALSE(generation_path);
            }
        }
    }
}

// Test splitting of a small object. The split must be successful, but the entry
// must not be placed to the LargeCAS. The result of spliting must contain one
// blob.
// The object cannot be implicitly reconstructed.
template <ObjectType kType>
static void TestSmall(Storage const& storage) noexcept {
    SECTION("Small") {
        static constexpr bool kIsTree = IsTreeObject(kType);
        static constexpr bool kIsExec = IsExecutableObject(kType);

        using TestType = std::conditional_t<kIsTree,
                                            LargeTestUtils::Tree,
                                            LargeTestUtils::Blob<kIsExec>>;

        auto const& cas = storage.CAS();

        // Create a small object:
        auto object = TestType::Create(
            cas, std::string(TestType::kSmallId), TestType::kSmallSize);
        CHECK(object);
        auto const& [digest, path] = *object;

        // Split the small object:
        auto pack_1 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        CHECK(pack_1);
        CHECK(pack_1->size() == 1);
        CHECK_FALSE(pack_1->front().IsTree());

        // Test that there is no large entry in the storage:
        // To ensure there is no split of the initial object, it is removed:
        CHECK(FileSystemManager::RemoveFile(path));
        CHECK_FALSE(FileSystemManager::IsFile(path));

        // The part of a small executable is the same file but without the
        // execution permission. It must be deleted too.
        if constexpr (kIsExec) {
            auto part_path = cas.BlobPath(pack_1->front(), false);
            CHECK(part_path);
            CHECK(FileSystemManager::RemoveFile(*part_path));
        }

        // Split must not find the large entry:
        auto pack_2 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        CHECK_FALSE(pack_2);
        CHECK(pack_2.error().Code() == LargeObjectErrorCode::FileNotFound);

        // There must be no spliced file:
        CHECK_FALSE(FileSystemManager::IsFile(path));

        // Check implicit splice fails:
        auto spliced_path =
            kIsTree ? cas.TreePath(digest) : cas.BlobPath(digest, kIsExec);
        CHECK_FALSE(spliced_path);
    }
}

// Test splitting of an empty object. The split must be successful, but the
// entry must not be placed to the LargeCAS. The result of splitting must be
// empty.
// The object cannot be implicitly reconstructed.
template <ObjectType kType>
static void TestEmpty(Storage const& storage) noexcept {
    SECTION("Empty") {
        static constexpr bool kIsTree = IsTreeObject(kType);
        static constexpr bool kIsExec = IsExecutableObject(kType);

        using TestType = std::conditional_t<kIsTree,
                                            LargeTestUtils::Tree,
                                            LargeTestUtils::Blob<kIsExec>>;

        // Create an empty file:
        auto temp_path = LargeTestUtils::Blob</*kIsExec=*/false>::Generate(
            std::string(TestType::kEmptyId), TestType::kEmptySize);
        REQUIRE(temp_path);

        auto const& cas = storage.CAS();
        auto digest = kIsTree ? cas.StoreTree(*temp_path)
                              : cas.StoreBlob(*temp_path, kIsExec);
        REQUIRE(digest);

        auto path =
            kIsTree ? cas.TreePath(*digest) : cas.BlobPath(*digest, kIsExec);
        REQUIRE(path);

        // Split the empty object:
        auto pack_1 = kIsTree ? cas.SplitTree(*digest) : cas.SplitBlob(*digest);
        CHECK(pack_1);
        CHECK(pack_1->empty());

        // Test that there is no large entry in the storage:
        // To ensure there is no split of the initial object, it is removed:
        CHECK(FileSystemManager::RemoveFile(*path));
        CHECK_FALSE(FileSystemManager::IsFile(*path));

        // Split must not find the large entry:
        auto pack_2 = kIsTree ? cas.SplitTree(*digest) : cas.SplitBlob(*digest);
        CHECK_FALSE(pack_2);
        CHECK(pack_2.error().Code() == LargeObjectErrorCode::FileNotFound);

        // There must be no spliced file:
        CHECK_FALSE(FileSystemManager::IsFile(*path));

        // Check implicit splice fails:
        auto spliced_path =
            kIsTree ? cas.TreePath(*digest) : cas.BlobPath(*digest, kIsExec);
        CHECK_FALSE(spliced_path);
    }
}

// Test splicing from an external source.
// 1. The object can be explicitly spliced, if the parts are presented in the
// storage.
// 2. Explicit splice fails, it the result of splicing is different from
// what was expected.
// 3. Explicit splice fails, if some parts of the tree are missing.
template <ObjectType kType>
static void TestExternal(StorageConfig const& storage_config,
                         Storage const& storage) noexcept {
    SECTION("External") {
        static constexpr bool kIsTree = IsTreeObject(kType);
        static constexpr bool kIsExec = IsExecutableObject(kType);

        using TestType = std::conditional_t<kIsTree,
                                            LargeTestUtils::Tree,
                                            LargeTestUtils::Blob<kIsExec>>;

        auto const& cas = storage.CAS();

        // Create a large object:
        auto object = TestType::Create(
            cas, std::string(TestType::kLargeId), TestType::kLargeSize);
        CHECK(object);
        auto const& [digest, path] = *object;

        // Split the object:
        auto pack_1 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        CHECK(pack_1);
        CHECK(pack_1->size() > 1);

        // External source is emulated by moving the large entry to an older
        // generation and promoting the parts of the entry to the youngest
        // generation:
        REQUIRE(GarbageCollector::TriggerGarbageCollection(storage_config));
        for (auto const& part : *pack_1) {
            static constexpr bool is_executable = false;
            REQUIRE(cas.BlobPath(part, is_executable));
        }

        auto const youngest = ::Generation::Create(&storage_config);

        SECTION("Proper request") {
            if constexpr (kIsTree) {
                // Promote the parts of the tree:
                auto splice = cas.TreePath(digest);
                REQUIRE(splice);
                REQUIRE(FileSystemManager::RemoveFile(*splice));
            }
            REQUIRE_FALSE(FileSystemManager::IsFile(path));

            // Reconstruct the result from parts:
            std::ignore =
                kIsTree ? youngest.CAS().SpliceTree(digest, *pack_1)
                        : youngest.CAS().SpliceBlob(digest, *pack_1, kIsExec);
            CHECK(FileSystemManager::IsFile(path));
        }

        // Simulate a situation when parts result to an existing file, but it is
        // not the expected result:
        SECTION("Digest consistency fail") {
            // Splice the result to check it will not be affected:
            auto implicit_splice =
                kIsTree ? cas.TreePath(digest) : cas.BlobPath(digest, kIsExec);
            REQUIRE(implicit_splice);
            REQUIRE(*implicit_splice == path);

            // Randomize one more object to simulate invalidation:
            auto small = TestType::Create(
                cas, std::string(TestType::kSmallId), TestType::kSmallSize);
            REQUIRE(small);
            auto const& [small_digest, small_path] = *small;

            // The entry itself is not important, only it's digest is needed:
            REQUIRE(FileSystemManager::RemoveFile(small_path));
            REQUIRE_FALSE(FileSystemManager::IsFile(small_path));

            // Invalidation is simulated by reconstructing the small_digest
            // object from the parts of the initial object:
            auto splice =
                kIsTree
                    ? youngest.CAS().SpliceTree(small_digest, *pack_1)
                    : youngest.CAS().SpliceBlob(small_digest, *pack_1, kIsExec);
            REQUIRE_FALSE(splice);
            CHECK(splice.error().Code() == LargeObjectErrorCode::InvalidResult);

            // The initial entry must not be affected:
            REQUIRE(FileSystemManager::IsFile(path));
        }

        if (kIsTree and not ProtocolTraits::Instance().IsCompatible()) {
            // Tree invariants check is omitted in compatible mode.
            SECTION("Tree invariants check fails") {
                // Check splice fails due to the tree invariants check.
                auto splice = youngest.CAS().SpliceTree(digest, *pack_1);
                REQUIRE_FALSE(splice);
                CHECK(splice.error().Code() ==
                      LargeObjectErrorCode::InvalidTree);
            }
        }
    }
}

// Test compactification of a storage generation.
// If there are objects in the storage that have an entry in
// the large CAS, they must be deleted during compactification.
// All splitable objects in the generation must be split.
template <ObjectType kType>
static void TestCompactification(StorageConfig const& storage_config,
                                 Storage const& storage) {
    SECTION("Compactify") {
        static constexpr bool kIsTree = IsTreeObject(kType);
        static constexpr bool kIsExec = IsExecutableObject(kType);

        using TestType = std::conditional_t<kIsTree,
                                            LargeTestUtils::Tree,
                                            LargeTestUtils::Blob<kIsExec>>;

        auto const& cas = storage.CAS();

        // Create a large object and split it:
        auto object = TestType::Create(
            cas, std::string(TestType::kLargeId), TestType::kLargeSize);
        REQUIRE(object);
        auto& [digest, path] = *object;
        auto result = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        REQUIRE(result);

        // For trees the size must be increased to exceed the internal
        // compactification threshold:
        static constexpr auto ExceedThresholdSize =
            kIsTree ? TestType::kLargeSize * 8 : TestType::kLargeSize;

        // Create a large object that is to be split during compactification:
        auto object_2 = TestType::Create(
            cas, std::string(TestType::kLargeId) + "_2", ExceedThresholdSize);
        REQUIRE(object_2);
        auto& [digest_2, path_2] = *object_2;

        // After an interruption of a build process intermediate unique files
        // may be present in the storage. To ensure compactification deals with
        // them properly, a "unique" file is created:
        auto invalid_object = TestType::Create(
            cas, std::string(TestType::kLargeId) + "_3", ExceedThresholdSize);
        REQUIRE(invalid_object);
        auto& [invalid_digest, invalid_path] = *invalid_object;

        auto unique_path = CreateUniquePath(invalid_path);
        REQUIRE(unique_path);
        REQUIRE(FileSystemManager::Rename(invalid_path, *unique_path));

        // Ensure all entries are in the storage:
        auto get_path = [](auto const& cas, ArtifactDigest const& digest) {
            return kIsTree ? cas.TreePath(digest)
                           : cas.BlobPath(digest, kIsExec);
        };

        auto const latest = ::Generation::Create(&storage_config);

        REQUIRE(get_path(latest.CAS(), digest).has_value());
        REQUIRE(get_path(latest.CAS(), digest_2).has_value());
        REQUIRE(FileSystemManager::IsFile(*unique_path));

        // Compactify the youngest generation:
        // Generation rotation is disabled to exclude uplinking.
        static constexpr bool kNoRotation = true;
        REQUIRE(GarbageCollector::TriggerGarbageCollection(storage_config,
                                                           kNoRotation));

        // All entries must be deleted during compactification, and for blobs
        // and executables there are no synchronized entries in the storage:
        REQUIRE_FALSE(get_path(latest.CAS(), digest).has_value());
        REQUIRE_FALSE(get_path(latest.CAS(), digest_2).has_value());
        REQUIRE_FALSE(FileSystemManager::IsFile(*unique_path));

        // All valid entries must be implicitly spliceable:
        REQUIRE(get_path(cas, digest).has_value());
        REQUIRE(get_path(cas, digest_2).has_value());
    }
}

TEST_CASE("LocalCAS: Split-Splice", "[storage]") {
    auto const config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&config.Get());

    SECTION("File") {
        TestLarge<ObjectType::File>(config.Get(), storage);
        TestSmall<ObjectType::File>(storage);
        TestEmpty<ObjectType::File>(storage);
        TestExternal<ObjectType::File>(config.Get(), storage);
        TestCompactification<ObjectType::File>(config.Get(), storage);
    }
    SECTION("Tree") {
        TestLarge<ObjectType::Tree>(config.Get(), storage);
        TestSmall<ObjectType::Tree>(storage);
        TestEmpty<ObjectType::Tree>(storage);
        TestExternal<ObjectType::Tree>(config.Get(), storage);
        TestCompactification<ObjectType::Tree>(config.Get(), storage);
    }
    SECTION("Executable") {
        TestLarge<ObjectType::Executable>(config.Get(), storage);
        TestSmall<ObjectType::Executable>(storage);
        TestEmpty<ObjectType::Executable>(storage);
        TestExternal<ObjectType::Executable>(config.Get(), storage);
        TestCompactification<ObjectType::Executable>(config.Get(), storage);
    }
}

// Test uplinking of nested large objects:
// A large tree depends on a number of nested objects:
//
// large_tree
// | - nested_blob
// | - nested_tree
// |    |- other nested entries
// | - other entries
//
// All large entries are preliminarily split and the spliced results are
// deleted. The youngest generation is empty. Uplinking must restore the
// object(and it's parts) and uplink them properly.
TEST_CASE("LargeObjectCAS: uplink nested large objects", "[storage]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const& cas = storage.CAS();

    // Randomize a large directory:
    auto tree_path = LargeTestUtils::Tree::Generate(
        std::string("nested_tree"), LargeTestUtils::Tree::kLargeSize);
    REQUIRE(tree_path);

    // Randomize a large nested tree:
    auto const nested_tree = (*tree_path) / "nested_tree";
    REQUIRE(LargeObjectUtils::GenerateDirectory(
        nested_tree, LargeTestUtils::Tree::kLargeSize));

    // Randomize a large nested blob:
    auto nested_blob = (*tree_path) / "nested_blob";
    REQUIRE(LargeObjectUtils::GenerateFile(nested_blob,
                                           LargeTestUtils::File::kLargeSize));

    // Add the nested tree to the CAS:
    auto nested_tree_digest = LargeTestUtils::Tree::StoreRaw(cas, nested_tree);
    REQUIRE(nested_tree_digest);
    auto nested_tree_path = cas.TreePath(*nested_tree_digest);
    REQUIRE(nested_tree_path);

    // Add the nested blob to the CAS:
    auto nested_blob_digest = cas.StoreBlob(nested_blob, false);
    REQUIRE(nested_blob_digest);
    auto nested_blob_path = cas.BlobPath(*nested_blob_digest, false);
    REQUIRE(nested_blob_path);

    // Add the initial large directory to the CAS:
    auto large_tree_digest = LargeTestUtils::Tree::StoreRaw(cas, *tree_path);
    REQUIRE(large_tree_digest);
    auto large_tree_path = cas.TreePath(*large_tree_digest);
    REQUIRE(large_tree_path);

    // Split large entries:
    auto split_nested_tree = cas.SplitTree(*nested_tree_digest);
    REQUIRE(split_nested_tree);

    auto split_nested_blob = cas.SplitBlob(*nested_blob_digest);
    REQUIRE(split_nested_blob);

    auto split_large_tree = cas.SplitTree(*large_tree_digest);
    REQUIRE(split_large_tree);

    // Remove the spliced results:
    REQUIRE(FileSystemManager::RemoveFile(*nested_tree_path));
    REQUIRE(FileSystemManager::RemoveFile(*nested_blob_path));
    REQUIRE(FileSystemManager::RemoveFile(*large_tree_path));

    // Rotate generations:
    REQUIRE(GarbageCollector::TriggerGarbageCollection(storage_config.Get()));

    // Ask to splice the large tree:
    auto result_path = cas.TreePath(*large_tree_digest);
    REQUIRE(result_path);

    // Only the main object must be reconstructed:
    CHECK(FileSystemManager::IsFile(*large_tree_path));

    // It's parts must not be reconstructed by default:
    CHECK_FALSE(FileSystemManager::IsFile(*nested_tree_path));
    CHECK_FALSE(FileSystemManager::IsFile(*nested_blob_path));

    auto const latest = ::Generation::Create(&storage_config.Get());

    // However, in native mode they might be reconstructed on request because
    // their entries are in the latest generation:
    if (not ProtocolTraits::Instance().IsCompatible()) {
        auto split_nested_tree_2 = latest.CAS().SplitTree(*nested_tree_digest);
        REQUIRE(split_nested_tree_2);

        auto split_nested_blob_2 = latest.CAS().SplitBlob(*nested_blob_digest);
        REQUIRE(split_nested_blob_2);
    }

    // Check there are no spliced results in old generations:
    for (std::size_t i = 1; i < storage_config.Get().num_generations; ++i) {
        auto const storage =
            ::Generation::Create(&storage_config.Get(), /*generation=*/i);
        auto const& generation_cas = storage.CAS();
        REQUIRE_FALSE(generation_cas.TreePath(*nested_tree_digest));
        REQUIRE_FALSE(generation_cas.TreePath(*large_tree_digest));
        REQUIRE_FALSE(generation_cas.BlobPath(*nested_blob_digest,
                                              /*is_executable=*/false));
    }
}
namespace {

/// \brief Extends the lifetime of large files for the whole set of tests.
class TestFilesDirectory final {
  public:
    [[nodiscard]] static auto Instance() noexcept -> TestFilesDirectory const& {
        static TestFilesDirectory directory;
        return directory;
    }
    [[nodiscard]] auto GetPath() const noexcept -> std::filesystem::path {
        return temp_directory_->GetPath();
    }

  private:
    TmpDirPtr temp_directory_;
    explicit TestFilesDirectory() noexcept {
        auto test_dir = FileSystemManager::GetCurrentDirectory() / "tmp";
        temp_directory_ = TmpDir::Create(test_dir / "tmp_space");
    }
};

namespace LargeTestUtils {
template <bool IsExecutable>
auto Blob<IsExecutable>::Create(LocalCAS<kDefaultDoGlobalUplink> const& cas,
                                std::string const& id,
                                std::uintmax_t size) noexcept
    -> std::optional<std::pair<ArtifactDigest, std::filesystem::path>> {
    auto path = Generate(id, size);
    auto digest = path ? cas.StoreBlob(*path, IsExecutable) : std::nullopt;
    auto blob_path =
        digest ? cas.BlobPath(*digest, IsExecutable) : std::nullopt;
    if (digest and blob_path) {
        return std::make_pair(std::move(*digest), std::move(*blob_path));
    }
    return std::nullopt;
}

template <bool IsExecutable>
auto Blob<IsExecutable>::Generate(std::string const& id,
                                  std::uintmax_t size) noexcept
    -> std::optional<std::filesystem::path> {
    std::string const path_id = "blob" + id;
    auto path = TestFilesDirectory::Instance().GetPath() / path_id;
    if (FileSystemManager::IsFile(path) or
        LargeObjectUtils::GenerateFile(path, size)) {
        return path;
    }
    return std::nullopt;
}

auto Tree::Create(LocalCAS<kDefaultDoGlobalUplink> const& cas,
                  std::string const& id,
                  std::uintmax_t entries_count) noexcept
    -> std::optional<std::pair<ArtifactDigest, std::filesystem::path>> {
    auto path = Generate(id, entries_count);
    auto digest = path ? StoreRaw(cas, *path) : std::nullopt;
    auto cas_path = digest ? cas.TreePath(*digest) : std::nullopt;
    if (digest and cas_path) {
        return std::make_pair(std::move(*digest), std::move(*cas_path));
    }
    return std::nullopt;
}

auto Tree::Generate(std::string const& id,
                    std::uintmax_t entries_count) noexcept
    -> std::optional<std::filesystem::path> {
    std::string const path_id = "tree" + id;
    auto path = TestFilesDirectory::Instance().GetPath() / path_id;
    if (FileSystemManager::IsDirectory(path) or
        LargeObjectUtils::GenerateDirectory(path, entries_count)) {
        return path;
    }
    return std::nullopt;
}

auto Tree::StoreRaw(LocalCAS<kDefaultDoGlobalUplink> const& cas,
                    std::filesystem::path const& directory) noexcept
    -> std::optional<ArtifactDigest> {
    if (not FileSystemManager::IsDirectory(directory)) {
        return std::nullopt;
    }

    auto store_blob = [&cas](std::filesystem::path const& path,
                             auto is_exec) -> std::optional<ArtifactDigest> {
        return cas.StoreBlob</*kOwner=*/true>(path, is_exec);
    };
    auto store_tree =
        [&cas](std::string const& content) -> std::optional<ArtifactDigest> {
        return cas.StoreTree(content);
    };
    auto store_symlink =
        [&cas](std::string const& content) -> std::optional<ArtifactDigest> {
        return cas.StoreBlob(content);
    };

    return ProtocolTraits::Instance().IsCompatible()
               ? BazelMsgFactory::CreateDirectoryDigestFromLocalTree(
                     directory, store_blob, store_tree, store_symlink)
               : BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
                     directory, store_blob, store_tree, store_symlink);
}
}  // namespace LargeTestUtils

}  // namespace
