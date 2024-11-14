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
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"
#include "test/utils/large_objects/large_object_utils.hpp"

namespace {
[[nodiscard]] auto GetTypedStorageConfig(StorageConfig const& config,
                                         HashFunction::Type hash_type)
    -> expected<StorageConfig, std::string>;

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

/// \brief Deeply rehash tree that is present in source storage and add it to
/// the target storage.
[[nodiscard]] auto StoreRehashedTree(Storage const& source,
                                     Storage const& target,
                                     ArtifactDigest const& tree)
    -> std::optional<ArtifactDigest>;
}  // namespace

TEST_CASE("Rehash tree", "[common]") {
    // Read storage config from the environment:
    auto const env_config = TestStorageConfig::Create();

    // Deploy native storage:
    auto const native_config =
        GetTypedStorageConfig(env_config.Get(), HashFunction::Type::GitSHA1);
    REQUIRE(native_config);
    auto const native_storage = Storage::Create(&*native_config);

    // Deploy compatible storage:
    auto const compatible_config = GetTypedStorageConfig(
        env_config.Get(), HashFunction::Type::PlainSHA256);
    REQUIRE(compatible_config);
    auto const compatible_storage = Storage::Create(&*compatible_config);

    // Randomize test directory:
    auto const test_dir = GenerateTestDirectory();
    REQUIRE(test_dir.has_value());
    auto const& test_dir_path = (*test_dir)->GetPath();

    auto const check_rehash = [&test_dir_path](Storage const& source,
                                               Storage const& target) -> void {
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
        auto const rehashed = StoreRehashedTree(source, target, *stored_digest);
        REQUIRE(rehashed.has_value());
        CHECK(rehashed->hash() == expected_rehashed->hash());
    };

    SECTION("GitTree to bazel::Directory") {
        check_rehash(native_storage, compatible_storage);
    }
}

namespace {
[[nodiscard]] auto GetTypedStorageConfig(StorageConfig const& config,
                                         HashFunction::Type hash_type)
    -> expected<StorageConfig, std::string> {
    return StorageConfig::Builder{}
        .SetBuildRoot(config.build_root)
        .SetHashType(hash_type)
        .Build();
}

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

[[nodiscard]] auto StoreRehashedTree(Storage const& source,
                                     Storage const& target,
                                     ArtifactDigest const& tree)
    -> std::optional<ArtifactDigest> {
    BazelMsgFactory::GitReadFunc const read_git =
        [&source](ArtifactDigest const& digest, ObjectType type)
        -> std::optional<std::variant<std::filesystem::path, std::string>> {
        return IsTreeObject(type)
                   ? source.CAS().TreePath(digest)
                   : source.CAS().BlobPath(digest, IsExecutableObject(type));
    };

    BazelMsgFactory::BlobStoreFunc const store_file =
        [&target](std::variant<std::filesystem::path, std::string> const& data,
                  bool is_exec) -> std::optional<ArtifactDigest> {
        if (std::holds_alternative<std::filesystem::path>(data)) {
            return target.CAS().StoreBlob</*kOwner=*/true>(
                std::get<std::filesystem::path>(data), is_exec);
        }
        if (std::holds_alternative<std::string>(data)) {
            return target.CAS().StoreBlob</*kOwner=*/true>(
                std::get<std::string>(data), is_exec);
        }
        return std::nullopt;
    };

    BazelMsgFactory::TreeStoreFunc const store_tree =
        [&target](std::string const& content) -> std::optional<ArtifactDigest> {
        return target.CAS().StoreTree(content);
    };

    BazelMsgFactory::SymlinkStoreFunc const store_symlink =
        [&target](std::string const& content) -> std::optional<ArtifactDigest> {
        return target.CAS().StoreBlob(content);
    };

    // Emulate rehash mapping using a regular std::unordered_map:
    std::unordered_map<ArtifactDigest, Artifact::ObjectInfo> rehash_map;

    BazelMsgFactory::RehashedDigestReadFunc const read_rehashed =
        [&rehash_map](ArtifactDigest const& digest)
        -> std::optional<Artifact::ObjectInfo> {
        auto const it = rehash_map.find(digest);
        if (it == rehash_map.end()) {
            return std::nullopt;
        }
        return it->second;
    };

    BazelMsgFactory::RehashedDigestStoreFunc const store_rehashed =
        [&rehash_map](ArtifactDigest const& key,
                      ArtifactDigest const& value,
                      ObjectType type) mutable -> std::optional<std::string> {
        rehash_map[key] = Artifact::ObjectInfo{.digest = value, .type = type};
        return std::nullopt;
    };

    REQUIRE(not ProtocolTraits::IsNative(target.GetHashFunction().GetType()));
    auto result =
        BazelMsgFactory::CreateDirectoryDigestFromGitTree(tree,
                                                          read_git,
                                                          store_file,
                                                          store_tree,
                                                          store_symlink,
                                                          read_rehashed,
                                                          store_rehashed);
    if (result) {
        return *std::move(result);
    }
    return std::nullopt;
}
}  // namespace
