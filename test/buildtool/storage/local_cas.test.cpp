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
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "test/utils/hermeticity/local.hpp"

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalCAS: Add blob to storage from bytes",
                 "[storage]") {
    std::string test_bytes("test");

    auto const& cas = Storage::Instance().CAS();
    auto test_digest = ArtifactDigest::Create<ObjectType::File>(test_bytes);

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

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalCAS: Add blob to storage from non-executable file",
                 "[storage]") {
    std::filesystem::path non_exec_file{
        "test/buildtool/storage/data/non_executable_file"};

    auto const& cas = Storage::Instance().CAS();
    auto test_blob = CreateBlobFromPath(non_exec_file);
    REQUIRE(test_blob);

    // check blob not in storage
    CHECK(not cas.BlobPath(test_blob->digest, true));
    CHECK(not cas.BlobPath(test_blob->digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not cas.BlobPath(test_blob->digest, true));
    CHECK(not cas.BlobPath(test_blob->digest, false));

    SECTION("Add non-executable blob to storage") {
        CHECK(cas.StoreBlob(non_exec_file, false));

        auto file_path = cas.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add executable blob to storage") {
        CHECK(cas.StoreBlob(non_exec_file, true));

        auto file_path = cas.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalCAS: Add blob to storage from executable file",
                 "[storage]") {
    std::filesystem::path exec_file{
        "test/buildtool/storage/data/executable_file"};

    auto const& cas = Storage::Instance().CAS();
    auto test_blob = CreateBlobFromPath(exec_file);
    REQUIRE(test_blob);

    // check blob not in storage
    CHECK(not cas.BlobPath(test_blob->digest, true));
    CHECK(not cas.BlobPath(test_blob->digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not cas.BlobPath(test_blob->digest, true));
    CHECK(not cas.BlobPath(test_blob->digest, false));

    SECTION("Add non-executable blob to storage") {
        CHECK(cas.StoreBlob(exec_file, false));

        auto file_path = cas.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add executable blob to storage") {
        CHECK(cas.StoreBlob(exec_file, true));

        auto file_path = cas.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = cas.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }
}
