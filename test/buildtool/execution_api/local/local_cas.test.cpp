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

#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "test/utils/hermeticity/local.hpp"

TEST_CASE_METHOD(HermeticLocalTestFixture, "LocalCAS", "[execution_api]") {
    std::string test_content{"test"};
    auto test_digest = ArtifactDigest::Create(test_content);

    SECTION("CAS for files") {
        LocalCAS<ObjectType::File> cas{};
        CHECK(not cas.BlobPath(test_digest));

        SECTION("Add blob from bytes and verify") {
            // add blob
            auto cas_digest = cas.StoreBlobFromBytes(test_content);
            CHECK(cas_digest);
            CHECK(std::equal_to<bazel_re::Digest>{}(*cas_digest, test_digest));

            // verify blob
            auto blob_path = cas.BlobPath(*cas_digest);
            REQUIRE(blob_path);
            auto const cas_content = FileSystemManager::ReadFile(*blob_path);
            CHECK(cas_content.has_value());
            CHECK(cas_content == test_content);
            CHECK(not FileSystemManager::IsExecutable(*blob_path));
        }

        SECTION("Add blob from file") {
            CHECK(FileSystemManager::CreateDirectory("tmp"));
            CHECK(FileSystemManager::WriteFile(test_content, "tmp/test"));

            // add blob
            auto cas_digest = cas.StoreBlobFromFile("tmp/test");
            CHECK(cas_digest);
            CHECK(std::equal_to<bazel_re::Digest>{}(*cas_digest, test_digest));

            // verify blob
            auto blob_path = cas.BlobPath(*cas_digest);
            REQUIRE(blob_path);
            auto const cas_content = FileSystemManager::ReadFile(*blob_path);
            CHECK(cas_content.has_value());
            CHECK(cas_content == test_content);
            CHECK(not FileSystemManager::IsExecutable(*blob_path));
        }
    }

    SECTION("CAS for executables") {
        LocalCAS<ObjectType::Executable> cas{};
        CHECK(not cas.BlobPath(test_digest));

        SECTION("Add blob from bytes and verify") {
            // add blob
            auto cas_digest = cas.StoreBlobFromBytes(test_content);
            CHECK(cas_digest);
            CHECK(std::equal_to<bazel_re::Digest>{}(*cas_digest, test_digest));

            // verify blob
            auto blob_path = cas.BlobPath(*cas_digest);
            REQUIRE(blob_path);
            auto const cas_content = FileSystemManager::ReadFile(*blob_path);
            CHECK(cas_content.has_value());
            CHECK(cas_content == test_content);
            CHECK(FileSystemManager::IsExecutable(*blob_path));
        }

        SECTION("Add blob from file") {
            CHECK(FileSystemManager::CreateDirectory("tmp"));
            CHECK(FileSystemManager::WriteFile(test_content, "tmp/test"));

            // add blob
            auto cas_digest = cas.StoreBlobFromFile("tmp/test");
            CHECK(cas_digest);
            CHECK(std::equal_to<bazel_re::Digest>{}(*cas_digest, test_digest));

            // verify blob
            auto blob_path = cas.BlobPath(*cas_digest);
            REQUIRE(blob_path);
            auto const cas_content = FileSystemManager::ReadFile(*blob_path);
            CHECK(cas_content.has_value());
            CHECK(cas_content == test_content);
            CHECK(FileSystemManager::IsExecutable(*blob_path));
        }
    }
}
