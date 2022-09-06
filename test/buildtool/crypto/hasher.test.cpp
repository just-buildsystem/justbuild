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

#include "catch2/catch.hpp"
#include "src/buildtool/crypto/hasher.hpp"

template <Hasher::HashType type>
void test_increment_hash(std::string const& bytes, std::string const& result) {
    Hasher hasher{type};
    hasher.Update(bytes.substr(0, bytes.size() / 2));
    hasher.Update(bytes.substr(bytes.size() / 2));
    auto digest = std::move(hasher).Finalize();
    CHECK(digest.HexString() == result);
}

TEST_CASE("Hasher", "[crypto]") {
    std::string bytes{"test"};

    SECTION("SHA-1") {
        // same as: echo -n test | sha1sum
        test_increment_hash<Hasher::HashType::SHA1>(
            bytes, "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
    }

    SECTION("SHA-256") {
        // same as: echo -n test | sha256sum
        test_increment_hash<Hasher::HashType::SHA256>(
            bytes,
            "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
    }

    SECTION("SHA-512") {
        // same as: echo -n test | sha512sum
        test_increment_hash<Hasher::HashType::SHA512>(
            bytes,
            "ee26b0dd4af7e749aa1a8ee3c10ae9923f618980772e473f8819a5d4940e0db27a"
            "c185f8a0e1d5f84f88bc887fd67b143732c304cc5fa9ad8e6f57f50028a8ff");
    }
}
