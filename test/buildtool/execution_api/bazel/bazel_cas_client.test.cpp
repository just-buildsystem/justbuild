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

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"

TEST_CASE("Bazel internals: CAS Client", "[execution_api]") {
    auto const& info = RemoteExecutionConfig::RemoteAddress();

    std::string instance_name{"remote-execution"};
    std::string content("test");

    // Create CAS client
    BazelCasClient cas_client(info->host, info->port);

    SECTION("Valid digest and blob") {
        // digest of "test"
        auto digest = ArtifactDigest::Create<ObjectType::File>(content);

        // Valid blob
        BazelBlob blob{digest, content, /*is_exec=*/false};

        // Search blob via digest
        auto digests = cas_client.FindMissingBlobs(instance_name, {digest});
        CHECK(digests.size() <= 1);

        if (!digests.empty()) {
            // Upload blob, if not found
            std::vector<BazelBlob> to_upload{blob};
            CHECK(cas_client.BatchUpdateBlobs(
                      instance_name, to_upload.begin(), to_upload.end()) == 1U);
        }

        // Read blob
        std::vector<bazel_re::Digest> to_read{digest};
        auto blobs = cas_client.BatchReadBlobs(
            instance_name, to_read.begin(), to_read.end());
        REQUIRE(blobs.size() == 1);
        CHECK(std::equal_to<bazel_re::Digest>{}(blobs[0].digest, digest));
        CHECK(blobs[0].data == content);
    }

    SECTION("Invalid digest and blob") {
        // Faulty digest
        bazel_re::Digest faulty_digest{};
        faulty_digest.set_hash(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        faulty_digest.set_size_bytes(4);

        // Faulty blob
        BazelBlob faulty_blob{faulty_digest, content, /*is_exec=*/false};

        // Search faulty digest
        CHECK(cas_client.FindMissingBlobs(instance_name, {faulty_digest})
                  .size() == 1);

        // Try upload faulty blob
        std::vector<BazelBlob> to_upload{faulty_blob};
        CHECK(cas_client.BatchUpdateBlobs(
                  instance_name, to_upload.begin(), to_upload.end()) == 0U);

        // Read blob via faulty digest
        std::vector<bazel_re::Digest> to_read{faulty_digest};
        CHECK(cas_client
                  .BatchReadBlobs(instance_name, to_read.begin(), to_read.end())
                  .empty());
    }
}
