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
    static constexpr auto* kEmpty = "";
    static constexpr auto* kInstanceName = "instance_name";
    static constexpr auto* kLongInstanceName = "multipart/instance_name";
    static constexpr auto* kVeryLongInstanceName =
        "some/very/long/multipart/instance_name";
    static constexpr auto* kInvalidInstanceName = "actions";
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

    std::string const request_empty =
        ByteStreamUtils::ReadRequest::ToString(kEmpty, digest);
    auto const parsed_empty =
        ByteStreamUtils::ReadRequest::FromString(request_empty);
    auto parsed_digest_empty = parsed_empty->GetDigest(hash_function.GetType());
    REQUIRE(parsed_empty);
    REQUIRE(parsed_digest_empty);
    CHECK(parsed_empty->GetInstanceName() == kEmpty);
    CHECK(parsed_digest_empty.value() == digest);

    std::string const request_long =
        ByteStreamUtils::ReadRequest::ToString(kLongInstanceName, digest);
    auto const parsed_long =
        ByteStreamUtils::ReadRequest::FromString(request_long);
    auto parsed_digest_long = parsed_long->GetDigest(hash_function.GetType());
    REQUIRE(parsed_long);
    REQUIRE(parsed_digest_long);
    CHECK(parsed_long->GetInstanceName() == kLongInstanceName);
    CHECK(parsed_digest_long.value() == digest);

    std::string const request_very_long =
        ByteStreamUtils::ReadRequest::ToString(kVeryLongInstanceName, digest);
    auto const parsed_very_long =
        ByteStreamUtils::ReadRequest::FromString(request_very_long);
    auto parsed_digest_very_long =
        parsed_very_long->GetDigest(hash_function.GetType());
    REQUIRE(parsed_very_long);
    REQUIRE(parsed_digest_very_long);
    CHECK(parsed_very_long->GetInstanceName() == kVeryLongInstanceName);
    CHECK(parsed_digest_long.value() == digest);

    std::string const request_invalid =
        ByteStreamUtils::ReadRequest::ToString(kInvalidInstanceName, digest);
    auto const parsed_invalid =
        ByteStreamUtils::ReadRequest::FromString(request_invalid);
    CHECK(parsed_invalid == std::nullopt);
}

TEST_CASE("WriteRequest", "[common]") {
    static constexpr auto* kEmpty = "";
    static constexpr auto* kInstanceName = "instance_name";
    static constexpr auto* kLongInstanceName = "multipart/instance_name";
    static constexpr auto* kVeryLongInstanceName =
        "some/very/long/multipart/instance_name";
    static constexpr auto* kInvalidInstanceName = "actions";
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

    std::string const request_empty =
        ByteStreamUtils::WriteRequest::ToString(kEmpty, uuid, digest);
    auto const parsed_empty =
        ByteStreamUtils::WriteRequest::FromString(request_empty);
    auto parsed_digest_empty = parsed_empty->GetDigest(hash_function.GetType());
    REQUIRE(parsed_empty);
    REQUIRE(parsed_digest_empty.has_value());
    CHECK(parsed_empty->GetInstanceName() == kEmpty);
    CHECK(parsed_empty->GetUUID() == uuid);
    CHECK(parsed_digest_empty.value() == digest);

    std::string const request_long = ByteStreamUtils::WriteRequest::ToString(
        kLongInstanceName, uuid, digest);
    auto const parsed_long =
        ByteStreamUtils::WriteRequest::FromString(request_long);
    auto parsed_digest_long = parsed_long->GetDigest(hash_function.GetType());
    REQUIRE(parsed_long);
    REQUIRE(parsed_digest_long.has_value());
    CHECK(parsed_long->GetInstanceName() == kLongInstanceName);
    CHECK(parsed_long->GetUUID() == uuid);
    CHECK(parsed_digest_long.value() == digest);

    std::string const request_very_long =
        ByteStreamUtils::WriteRequest::ToString(
            kVeryLongInstanceName, uuid, digest);
    auto const parsed_very_long =
        ByteStreamUtils::WriteRequest::FromString(request_very_long);
    auto parsed_digest_very_long =
        parsed_very_long->GetDigest(hash_function.GetType());
    REQUIRE(parsed_very_long);
    REQUIRE(parsed_digest_very_long.has_value());
    CHECK(parsed_very_long->GetInstanceName() == kVeryLongInstanceName);
    CHECK(parsed_very_long->GetUUID() == uuid);
    CHECK(parsed_digest_very_long.value() == digest);

    std::string const request_invalid = ByteStreamUtils::WriteRequest::ToString(
        kInvalidInstanceName, uuid, digest);
    auto const parsed_invalid =
        ByteStreamUtils::WriteRequest::FromString(request_invalid);
    CHECK(parsed_invalid == std::nullopt);
}
