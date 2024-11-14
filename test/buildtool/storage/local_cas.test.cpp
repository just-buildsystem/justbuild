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

#include <filesystem>
#include <optional>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

TEST_CASE("LocalCAS: Add blob to storage from bytes", "[storage]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const& cas = storage.CAS();

    std::string test_bytes("test");

    auto test_digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        storage_config.Get().hash_function, test_bytes);

    // check blob not in storage
    CHECK(not cas.BlobPath(test_digest, true));
    CHECK(not cas.BlobPath(test_digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not cas.BlobPath(test_digest, true));
    CHECK(not cas.BlobPath(test_digest, false));

    SECTION("Add non-executable blob to storage") {
        CHECK(cas.StoreBlob(test_bytes, false));

        auto file_path = cas.BlobPath(test_digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(test_digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add executable blob to storage") {
        CHECK(cas.StoreBlob(test_bytes, true));

        auto file_path = cas.BlobPath(test_digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(test_digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }
}

TEST_CASE("LocalCAS: Add blob to storage from non-executable file",
          "[storage]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const& cas = storage.CAS();

    std::filesystem::path non_exec_file{
        "test/buildtool/storage/data/non_executable_file"};

    auto test_blob = ArtifactDigestFactory::HashFileAs<ObjectType::File>(
        storage_config.Get().hash_function, non_exec_file);
    REQUIRE(test_blob);

    // check blob not in storage
    CHECK(not cas.BlobPath(*test_blob, true));
    CHECK(not cas.BlobPath(*test_blob, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not cas.BlobPath(*test_blob, true));
    CHECK(not cas.BlobPath(*test_blob, false));

    SECTION("Add non-executable blob to storage") {
        CHECK(cas.StoreBlob(non_exec_file, false));

        auto file_path = cas.BlobPath(*test_blob, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(*test_blob, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add executable blob to storage") {
        CHECK(cas.StoreBlob(non_exec_file, true));

        auto file_path = cas.BlobPath(*test_blob, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(*test_blob, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }
}

TEST_CASE("LocalCAS: Add blob to storage from executable file", "[storage]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const& cas = storage.CAS();

    std::filesystem::path exec_file{
        "test/buildtool/storage/data/executable_file"};

    auto test_blob = ArtifactDigestFactory::HashFileAs<ObjectType::File>(
        storage_config.Get().hash_function, exec_file);
    REQUIRE(test_blob);

    // check blob not in storage
    CHECK(not cas.BlobPath(*test_blob, true));
    CHECK(not cas.BlobPath(*test_blob, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not cas.BlobPath(*test_blob, true));
    CHECK(not cas.BlobPath(*test_blob, false));

    SECTION("Add non-executable blob to storage") {
        CHECK(cas.StoreBlob(exec_file, false));

        auto file_path = cas.BlobPath(*test_blob, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(*test_blob, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add executable blob to storage") {
        CHECK(cas.StoreBlob(exec_file, true));

        auto file_path = cas.BlobPath(*test_blob, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(*test_blob, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }
}
