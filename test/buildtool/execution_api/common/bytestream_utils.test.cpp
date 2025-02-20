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

#include "src/buildtool/execution_api/common/bytestream_utils.hpp"

#include <optional>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/ids.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/expected.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"

TEST_CASE("ReadRequest", "[common]") {
    static constexpr auto* kInstanceName = "instance_name";
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto const digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, "test_string");

    std::string const request =
        ByteStreamUtils::ReadRequest::ToString(kInstanceName, digest);
    auto const parsed = ByteStreamUtils::ReadRequest::FromString(request);
    auto parsed_digest = parsed->GetDigest(hash_function.GetType());
    REQUIRE(parsed);
    REQUIRE(parsed_digest);
    CHECK(parsed->GetInstanceName() == kInstanceName);
    CHECK(parsed_digest.value() == digest);
}

TEST_CASE("WriteRequest", "[common]") {
    static constexpr auto* kInstanceName = "instance_name";
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto id = CreateProcessUniqueId();
    REQUIRE(id);
    std::string const uuid = CreateUUIDVersion4(*id);

    auto const digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, "test_string");

    std::string const request =
        ByteStreamUtils::WriteRequest::ToString(kInstanceName, uuid, digest);
    auto const parsed = ByteStreamUtils::WriteRequest::FromString(request);
    auto parsed_digest = parsed->GetDigest(hash_function.GetType());
    REQUIRE(parsed);
    REQUIRE(parsed_digest.has_value());
    CHECK(parsed->GetInstanceName() == kInstanceName);
    CHECK(parsed->GetUUID() == uuid);
    CHECK(parsed_digest.value() == digest);
}
