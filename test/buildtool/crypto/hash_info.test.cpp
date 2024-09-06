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

#include "src/buildtool/crypto/hash_info.hpp"

#include <optional>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/crypto/hash_function.hpp"

inline constexpr auto kValidGitSHA1 =
    "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391";
inline constexpr auto kInvalidGitSHA1 =
    "e69de29bb2d1d6434b8b29ae775ad8c2e48c539z";
inline constexpr auto kValidPlainSHA256 =
    "2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae";
inline constexpr auto kInvalidPlainSHA256 =
    "2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7az";

TEST_CASE("Empty HashInfo", "[crypto]") {
    HashInfo info;
    CHECK_FALSE(info.Hash().empty());
    CHECK(info.HashType() == HashFunction::Type::GitSHA1);
    CHECK(info.IsTree() == false);
}

TEST_CASE("Native HashInfo", "[crypto]") {
    SECTION("Valid hash") {
        // Valid hex string of valid length
        CHECK(HashInfo::Create(HashFunction::Type::GitSHA1,
                               kValidGitSHA1,
                               /*is_tree=*/false));
        CHECK(HashInfo::Create(HashFunction::Type::GitSHA1,
                               kValidGitSHA1,
                               /*is_tree=*/true));
    }

    SECTION("Invalid hash") {
        // Invalid hex string (contains z)
        CHECK_FALSE(HashInfo::Create(HashFunction::Type::GitSHA1,
                                     kInvalidGitSHA1,
                                     /*is_tree=*/false));
        CHECK_FALSE(HashInfo::Create(HashFunction::Type::GitSHA1,
                                     kInvalidGitSHA1,
                                     /*is_tree=*/true));

        // Valid hex string, but wrong length
        CHECK_FALSE(HashInfo::Create(HashFunction::Type::GitSHA1,
                                     kValidPlainSHA256,
                                     /*is_tree=*/false));
        CHECK_FALSE(HashInfo::Create(HashFunction::Type::GitSHA1,
                                     kValidPlainSHA256,
                                     /*is_tree=*/true));
    }
}

TEST_CASE("Compatible HashInfo", "[crypto]") {
    SECTION("Valid hash") {
        // Valid hex string of valid length, not a tree
        CHECK(HashInfo::Create(HashFunction::Type::PlainSHA256,
                               kValidPlainSHA256,
                               /*is_tree=*/false));
    }

    SECTION("No trees") {
        // Valid hex string of valid length, a tree that is not allowed in
        // the compatible mode
        CHECK_FALSE(HashInfo::Create(HashFunction::Type::PlainSHA256,
                                     kValidPlainSHA256,
                                     /*is_tree=*/true));
    }

    SECTION("Invalid hash") {
        // Invalid hex string (contains z)
        CHECK_FALSE(HashInfo::Create(HashFunction::Type::PlainSHA256,
                                     kInvalidPlainSHA256,
                                     /*is_tree=*/false));

        // Valid hex string, but wrong length
        CHECK_FALSE(HashInfo::Create(HashFunction::Type::PlainSHA256,
                                     kValidGitSHA1,
                                     /*is_tree=*/false));
    }
}
