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

#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/common/content_blob_container.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"

TEST_CASE("Bazel internals: CAS Client", "[execution_api]") {
    std::string instance_name{"remote-execution"};
    std::string content("test");

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    // Create CAS client
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    RetryConfig retry_config{};  // default retry config
    BazelCasClient cas_client(remote_config->remote_address->host,
                              remote_config->remote_address->port,
                              &*auth_config,
                              &retry_config);

    SECTION("Valid digest and blob") {
        // digest of "test"
        HashFunction const hash_function{TestHashType::ReadFromEnvironment()};
        auto const digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
            hash_function, content);
        auto bazel_digest = BazelDigestFactory::HashDataAs<ObjectType::File>(
            hash_function, content);

        // Valid blob
        ArtifactBlob blob{digest, content, /*is_exec=*/false};

        // Search blob via digest
        auto digests =
            cas_client.FindMissingBlobs(instance_name, {bazel_digest});
        CHECK(digests.size() <= 1);

        if (not digests.empty()) {
            // Upload blob, if not found
            CHECK(cas_client.BatchUpdateBlobs(instance_name, {blob}) == 1U);
        }

        // Read blob
        auto blobs = cas_client.BatchReadBlobs(instance_name, {digest});
        REQUIRE(blobs.size() == 1);
        CHECK(blobs.begin()->digest == digest);
        CHECK(*blobs.begin()->data == content);
    }

    SECTION("Invalid digest and blob") {
        // Faulty digest
        bazel_re::Digest faulty_digest{};
        faulty_digest.set_hash(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        faulty_digest.set_size_bytes(4);

        // Search faulty digest
        CHECK(cas_client.FindMissingBlobs(instance_name, {faulty_digest})
                  .size() == 1);
    }
}
