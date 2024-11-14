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

#include "src/buildtool/crypto/hash_function.hpp"

#include <string>
#include <utility>  // std::move

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/crypto/hasher.hpp"

TEST_CASE("Hash Function", "[crypto]") {
    std::string bytes{"test"};

    SECTION("GitSHA1") {
        HashFunction const hash_function{HashFunction::Type::GitSHA1};

        // same as: echo -n test | sha1sum
        CHECK(hash_function.PlainHashData(bytes).HexString() ==
              "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
        // same as: echo -n test | git hash-object --stdin
        CHECK(hash_function.HashBlobData(bytes).HexString() ==
              "30d74d258442c7c65512eafab474568dd706c430");
        // same as: echo -n test | git hash-object -t "tree" --stdin --literally
        CHECK(hash_function.HashTreeData(bytes).HexString() ==
              "5f0ecc1a989593005e80f457446133250fcc43cc");

        auto hasher = hash_function.MakeHasher();
        hasher.Update(bytes);
        CHECK(std::move(hasher).Finalize().HexString() ==  // NOLINT
              "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
    }

    SECTION("PlainSHA256") {
        HashFunction const hash_function{HashFunction::Type::PlainSHA256};

        // all same as: echo -n test | sha256sum
        CHECK(
            hash_function.PlainHashData(bytes).HexString() ==
            "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
        CHECK(
            hash_function.HashBlobData(bytes).HexString() ==
            "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
        CHECK(
            hash_function.HashTreeData(bytes).HexString() ==
            "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");

        auto hasher = hash_function.MakeHasher();
        hasher.Update(bytes);
        CHECK(
            std::move(hasher).Finalize().HexString() ==  // NOLINT
            "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
    }
}
