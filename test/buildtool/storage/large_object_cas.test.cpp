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
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/large_object_cas.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "test/utils/hermeticity/local.hpp"
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
        -> std::optional<std::pair<bazel_re::Digest, std::filesystem::path>>;

    [[nodiscard]] static auto Generate(std::string const& id,
                                       std::uintmax_t size) noexcept
        -> std::optional<std::filesystem::path>;
};

class Tree final {
  public:
    static constexpr auto kLargeId = std::string_view("tree_256");
    static constexpr auto kLargeSize = std::uintmax_t(256);

    static constexpr auto kSmallId = std::string_view("tree_1");
    static constexpr auto kSmallSize = std::uintmax_t(1);

    static constexpr auto kEmptyId = std::string_view("tree_0");
    static constexpr auto kEmptySize = std::uintmax_t(0);

    [[nodiscard]] static auto Create(
        LocalCAS<kDefaultDoGlobalUplink> const& cas,
        std::string const& id,
        std::uintmax_t entries_count) noexcept
        -> std::optional<std::pair<bazel_re::Digest, std::filesystem::path>>;

    [[nodiscard]] static auto Generate(std::string const& id,
                                       std::uintmax_t entries_count) noexcept
        -> std::optional<std::filesystem::path>;

    [[nodiscard]] static auto StoreRaw(
        LocalCAS<kDefaultDoGlobalUplink> const& cas,
        std::filesystem::path const& directory) noexcept
        -> std::optional<bazel_re::Digest>;
};

}  // namespace LargeTestUtils
}  // namespace

// Test splitting of a small tree.
TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LargeObjectCAS: split a small tree",
                 "[storage]") {
    auto temp_dir = StorageConfig::CreateTypedTmpDir("large_object_cas");
    REQUIRE(temp_dir);

    auto const& cas = Storage::Instance().CAS();
    LargeObjectCAS<true, ObjectType::Tree> const large_cas(
        cas, temp_dir->GetPath() / "root_1");

    // Create a small tree:
    using LargeTestUtils::Tree;
    auto small =
        Tree::Create(cas, std::string(Tree::kSmallId), Tree::kSmallSize);
    REQUIRE(small);
    auto const& [digest, path] = *small;

    // Split must be successful:
    auto split_pack = large_cas.Split(digest);
    auto* parts = std::get_if<std::vector<bazel_re::Digest>>(&split_pack);
    REQUIRE(parts);

    // The result must contain one blob digest:
    CHECK(parts->size() == 1);
    CHECK_FALSE(NativeSupport::IsTree(parts->front().hash()));
}

// Test splitting of a large object. The split must be successful and the entry
// must be placed to the LargeCAS. The second split of the same object must load
// the result from the LargeCAS, no actual split must occur.
// The object can be implicitly reconstructed from the LargeCAS.
template <ObjectType kType>
static void TestLarge() noexcept {
    SECTION("Large") {
        static constexpr bool kIsTree = IsTreeObject(kType);
        static constexpr bool kIsExec = IsExecutableObject(kType);

        using TestType = std::conditional_t<kIsTree,
                                            LargeTestUtils::Tree,
                                            LargeTestUtils::Blob<kIsExec>>;

        auto const& cas = Storage::Instance().CAS();

        // Create a large object:
        auto object = TestType::Create(
            cas, std::string(TestType::kLargeId), TestType::kLargeSize);
        CHECK(object);
        auto const& [digest, path] = *object;

        // Split the large object:
        auto pack_1 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        auto* split = std::get_if<std::vector<bazel_re::Digest>>(&pack_1);
        CHECK(split);
        CHECK(split->size() > 1);

        CHECK(FileSystemManager::RemoveFile(path));
        CHECK_FALSE(FileSystemManager::IsFile(path));

        SECTION("Split short-circuting") {
            // Check the second call loads the entry from the large CAS:
            auto pack_2 =
                kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
            auto* split_2 = std::get_if<std::vector<bazel_re::Digest>>(&pack_2);
            CHECK(split_2);
            CHECK(split_2->size() == split->size());

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
    }
}

// Test splitting of a small object. The split must be successful, but the entry
// must not be placed to the LargeCAS. The result of spliting must contain one
// blob.
// The object cannot be implicitly reconstructed.
template <ObjectType kType>
static void TestSmall() noexcept {
    SECTION("Small") {
        static constexpr bool kIsTree = IsTreeObject(kType);
        static constexpr bool kIsExec = IsExecutableObject(kType);

        using TestType = std::conditional_t<kIsTree,
                                            LargeTestUtils::Tree,
                                            LargeTestUtils::Blob<kIsExec>>;

        auto const& cas = Storage::Instance().CAS();

        // Create a small object:
        auto object = TestType::Create(
            cas, std::string(TestType::kSmallId), TestType::kSmallSize);
        CHECK(object);
        auto const& [digest, path] = *object;

        // Split the small object:
        auto pack_1 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        auto* split = std::get_if<std::vector<bazel_re::Digest>>(&pack_1);
        CHECK(split);
        CHECK(split->size() == 1);
        CHECK_FALSE(NativeSupport::IsTree(split->front().hash()));

        // Test that there is no large entry in the storage:
        // To ensure there is no split of the initial object, it is removed:
        CHECK(FileSystemManager::RemoveFile(path));
        CHECK_FALSE(FileSystemManager::IsFile(path));

        // The part of a small executable is the same file but without the
        // execution permission. It must be deleted too.
        if constexpr (kIsExec) {
            auto part_path = cas.BlobPath(split->front(), false);
            CHECK(part_path);
            CHECK(FileSystemManager::RemoveFile(*part_path));
        }

        // Split must not find the large entry:
        auto pack_2 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        auto* error_2 = std::get_if<LargeObjectError>(&pack_2);
        CHECK(error_2);
        CHECK(error_2->Code() == LargeObjectErrorCode::FileNotFound);

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
static void TestEmpty() noexcept {
    SECTION("Empty") {
        static constexpr bool kIsTree = IsTreeObject(kType);
        static constexpr bool kIsExec = IsExecutableObject(kType);

        using TestType = std::conditional_t<kIsTree,
                                            LargeTestUtils::Tree,
                                            LargeTestUtils::Blob<kIsExec>>;

        auto const& cas = Storage::Instance().CAS();

        // Create an empty object:
        auto object = TestType::Create(
            cas, std::string(TestType::kEmptyId), TestType::kEmptySize);
        CHECK(object);
        auto const& [digest, path] = *object;

        // Split the empty object:
        auto pack_1 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        auto* split = std::get_if<std::vector<bazel_re::Digest>>(&pack_1);
        CHECK(split);
        CHECK(split->empty());

        // Test that there is no large entry in the storage:
        // To ensure there is no split of the initial object, it is removed:
        CHECK(FileSystemManager::RemoveFile(path));
        CHECK_FALSE(FileSystemManager::IsFile(path));

        // For executables the non-executable entry must be also deleted.
        if constexpr (kIsExec) {
            auto blob_path = cas.BlobPath(digest, /*is_executable=*/false);
            REQUIRE(blob_path);
            CHECK(FileSystemManager::RemoveFile(*blob_path));
            CHECK_FALSE(FileSystemManager::IsFile(*blob_path));
        }

        // Split must not find the large entry:
        auto pack_2 = kIsTree ? cas.SplitTree(digest) : cas.SplitBlob(digest);
        auto* error_2 = std::get_if<LargeObjectError>(&pack_2);
        CHECK(error_2);
        CHECK(error_2->Code() == LargeObjectErrorCode::FileNotFound);

        // There must be no spliced file:
        CHECK_FALSE(FileSystemManager::IsFile(path));

        // Check implicit splice fails:
        auto spliced_path =
            kIsTree ? cas.TreePath(digest) : cas.BlobPath(digest, kIsExec);
        CHECK_FALSE(spliced_path);
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalCAS: Split-Splice",
                 "[storage]") {
    SECTION("File") {
        TestLarge<ObjectType::File>();
        TestSmall<ObjectType::File>();
        TestEmpty<ObjectType::File>();
    }
    SECTION("Tree") {
        TestLarge<ObjectType::Tree>();
        TestSmall<ObjectType::Tree>();
        TestEmpty<ObjectType::Tree>();
    }
    SECTION("Executable") {
        TestLarge<ObjectType::Executable>();
        TestSmall<ObjectType::Executable>();
        TestEmpty<ObjectType::Executable>();
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
    -> std::optional<std::pair<bazel_re::Digest, std::filesystem::path>> {
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
    -> std::optional<std::pair<bazel_re::Digest, std::filesystem::path>> {
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
    -> std::optional<bazel_re::Digest> {
    if (not FileSystemManager::IsDirectory(directory)) {
        return std::nullopt;
    }

    auto store_blob = [&cas](auto const& path, auto is_exec) {
        return cas.StoreBlob(path, is_exec);
    };
    auto store_tree = [&cas](auto const& bytes, auto const& /*dir*/) {
        return cas.StoreTree(bytes);
    };
    auto store_symlink = [&cas](auto const& content) {
        return cas.StoreBlob(content);
    };

    return BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
        directory, store_blob, store_tree, store_symlink);
}
}  // namespace LargeTestUtils

}  // namespace
