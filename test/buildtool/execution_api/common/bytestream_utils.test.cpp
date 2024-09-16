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

#include <functional>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/bazel_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"

TEST_CASE("ReadRequest", "[common]") {
    static constexpr auto* kInstanceName = "instance_name";
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto const digest = BazelDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, "test_string");

    std::string const request =
        ByteStreamUtils::ReadRequest{kInstanceName, digest}.ToString();
    auto const parsed = ByteStreamUtils::ReadRequest::FromString(request);
    REQUIRE(parsed);
    CHECK(parsed->GetInstanceName().compare(kInstanceName) == 0);
    CHECK(std::equal_to<bazel_re::Digest>{}(parsed->GetDigest(), digest));
}

TEST_CASE("WriteRequest", "[common]") {
    static constexpr auto* kInstanceName = "instance_name";
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto id = CreateProcessUniqueId();
    REQUIRE(id);
    std::string const uuid = CreateUUIDVersion4(*id);

    auto const digest = BazelDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, "test_string");

    std::string const request =
        ByteStreamUtils::WriteRequest{kInstanceName, uuid, digest}.ToString();
    auto const parsed = ByteStreamUtils::WriteRequest::FromString(request);
    REQUIRE(parsed);
    CHECK(parsed->GetInstanceName().compare(kInstanceName) == 0);
    CHECK(parsed->GetUUID().compare(uuid) == 0);
    CHECK(std::equal_to<bazel_re::Digest>{}(parsed->GetDigest(), digest));
}
