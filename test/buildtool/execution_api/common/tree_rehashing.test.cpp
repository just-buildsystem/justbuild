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

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"
#include "test/utils/large_objects/large_object_utils.hpp"

namespace {
[[nodiscard]] auto GenerateTestDirectory() -> std::optional<TmpDirPtr>;

/// \brief Deeply hash a local tree and add it to the storage.
[[nodiscard]] auto StoreHashedTree(Storage const& storage,
                                   std::filesystem::path const& path) noexcept
    -> std::optional<ArtifactDigest>;

/// \brief Deeply hash a local tree, doesn't add anything to the
/// storage, just calls for ArtifactDigestFactory.
[[nodiscard]] auto HashTree(HashFunction::Type hash_type,
                            std::filesystem::path const& path) noexcept
    -> std::optional<ArtifactDigest>;
}  // namespace

TEST_CASE("Rehash tree", "[common]") {
    // Read storage config from the environment:
    auto const env_config = TestStorageConfig::Create();

    // Deploy native storage:
    auto const native_config = StorageConfig::Builder::Rebuild(env_config.Get())
                                   .SetHashType(HashFunction::Type::GitSHA1)
                                   .Build();
    REQUIRE(native_config);

    // Deploy compatible storage:
    auto const compatible_config =
        StorageConfig::Builder::Rebuild(env_config.Get())
            .SetHashType(HashFunction::Type::PlainSHA256)
            .Build();
    REQUIRE(compatible_config);

    // Randomize test directory:
    auto const test_dir = GenerateTestDirectory();
    REQUIRE(test_dir.has_value());
    auto const& test_dir_path = (*test_dir)->GetPath();

    auto const check_rehash = [&test_dir_path](
                                  StorageConfig const& source_config,
                                  StorageConfig const& target_config) -> void {
        auto const source = Storage::Create(&source_config);
        auto const target = Storage::Create(&target_config);

        // Add digest to the source storage:
        auto const stored_digest = StoreHashedTree(source, test_dir_path);
        REQUIRE(stored_digest.has_value());

        // calculate the "expected" after rehashing digest:
        auto const expected_rehashed =
            HashTree(target.GetHashFunction().GetType(), test_dir_path);
        REQUIRE(expected_rehashed.has_value());

        // Rehash source digest present in the source storage and add
        // it to the target storage. The resulting digest must be
        // equal to expected_rehashed.

        auto const rehashed = RehashUtils::RehashDigest(
            {Artifact::ObjectInfo{.digest = *stored_digest,
                                  .type = ObjectType::Tree}},
            source_config,
            target_config,
            /*apis=*/std::nullopt);
        REQUIRE(rehashed.has_value());
        CHECK(rehashed->front().digest.hash() == expected_rehashed->hash());
    };

    SECTION("GitTree to bazel::Directory") {
        check_rehash(*native_config, *compatible_config);
    }
    SECTION("bazel::Directory to GitTree") {
        check_rehash(*compatible_config, *native_config);
    }

    // Emulating the scenario when only the top-level bazel::Directory is
    // available locally, and to rehash it to a git_tree, the parts must be
    // downloaded from the remote endpoint.
    // In the context of this test, "remote" is not a real remote
    // endpoint: it is emulated using one more Storage that is deployed in a
    // temporary directory. This "remote" storage contains the whole
    // bazel::Directory and can be passed to ApiBundle's remote field to be used
    // for downloading of artifacts that are unknown to the local storage.
    SECTION("partially available bazel::Directory") {
        // Provide aliases to be clear in regard of the direction of rehashing:
        auto const& source_config = *compatible_config;
        auto const& target_config = *native_config;
        auto const source_storage = Storage::Create(&source_config);

        // Deploy one more "remote" storage:
        auto const tmp_dir = source_config.CreateTypedTmpDir("remote");
        auto const remote_config =
            StorageConfig::Builder{}
                .SetBuildRoot(tmp_dir->GetPath())
                .SetHashType(source_config.hash_function.GetType())
                .Build();
        REQUIRE(remote_config);
        auto remote_storage = Storage::Create(&remote_config.value());

        // Store the whole bazel::Directory to the "remote" storage:
        auto const stored_digest =
            StoreHashedTree(remote_storage, test_dir_path);
        REQUIRE(stored_digest.has_value());

        // Get the expected result of rehashing:
        auto const expected_rehashed =
            HashTree(target_config.hash_function.GetType(), test_dir_path);
        REQUIRE(expected_rehashed.has_value());

        // Add the top-level bazel::Directory only to the source storage:
        auto const top_tree_path =
            remote_storage.CAS().TreePath(stored_digest.value());
        REQUIRE(top_tree_path);
        auto source_top_tree_digest =
            source_storage.CAS().StoreTree(*top_tree_path);
        REQUIRE(source_top_tree_digest.has_value());
        REQUIRE(*source_top_tree_digest == *stored_digest);

        // Create parts of ApiBundle, taking into account that "remote" is a
        // LocalApi as well.
        LocalExecutionConfig const dump_exec_config{};
        LocalContext const local_context{.exec_config = &dump_exec_config,
                                         .storage_config = &source_config,
                                         .storage = &source_storage};
        LocalContext const remote_context{.exec_config = &dump_exec_config,
                                          .storage_config = &*remote_config,
                                          .storage = &remote_storage};
        ApiBundle const apis{
            .hash_function = local_context.storage_config->hash_function,
            .local = std::make_shared<LocalApi>(&local_context),
            .remote = std::make_shared<LocalApi>(&remote_context)};

        // Rehash the top-level directory. This operation requires "downloading"
        // of unknown parts of the trees from the "remote".
        auto const rehashed = RehashUtils::RehashDigest(
            {Artifact::ObjectInfo{.digest = *stored_digest,
                                  .type = ObjectType::Tree}},
            *compatible_config,
            *native_config,
            &apis);
        REQUIRE(rehashed.has_value());
        CHECK(rehashed->front().digest.hash() == expected_rehashed->hash());
    }
}

namespace {
[[nodiscard]] auto GenerateTestDirectory() -> std::optional<TmpDirPtr> {
    auto const test_dir = FileSystemManager::GetCurrentDirectory() / "tmp";
    auto head_temp_directory = TmpDir::Create(test_dir / "head_dir");
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
    return head_temp_directory;
}

[[nodiscard]] auto StoreHashedTree(Storage const& storage,
                                   std::filesystem::path const& path) noexcept
    -> std::optional<ArtifactDigest> {
    auto const& cas = storage.CAS();

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

    return ProtocolTraits::IsNative(cas.GetHashFunction().GetType())
               ? BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
                     path, store_blob, store_tree, store_symlink)
               : BazelMsgFactory::CreateDirectoryDigestFromLocalTree(
                     path, store_blob, store_tree, store_symlink);
}

[[nodiscard]] auto HashTree(HashFunction::Type hash_type,
                            std::filesystem::path const& path) noexcept
    -> std::optional<ArtifactDigest> {
    HashFunction const hash_function{hash_type};
    auto hash_blob = [hash_function](
                         std::filesystem::path const& path,
                         auto /*is_exec*/) -> std::optional<ArtifactDigest> {
        return ArtifactDigestFactory::HashFileAs<ObjectType::File>(
            hash_function, path);
    };
    auto hash_tree =
        [hash_function](
            std::string const& content) -> std::optional<ArtifactDigest> {
        return ArtifactDigestFactory::HashDataAs<ObjectType::Tree>(
            hash_function, content);
    };
    auto hash_symlink =
        [hash_function](
            std::string const& content) -> std::optional<ArtifactDigest> {
        return ArtifactDigestFactory::HashDataAs<ObjectType::File>(
            hash_function, content);
    };
    return ProtocolTraits::IsNative(hash_type)
               ? BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
                     path, hash_blob, hash_tree, hash_symlink)
               : BazelMsgFactory::CreateDirectoryDigestFromLocalTree(
                     path, hash_blob, hash_tree, hash_symlink);
}
}  // namespace
