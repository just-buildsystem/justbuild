// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/source_tree_client.hpp"

auto const kRootCommit =
    std::string{"e4fc610c60716286b98cf51ad0c8f0d50f3aebb5"};
auto const kRootId = std::string{"c610db170fbcad5f2d66fe19972495923f3b2536"};
auto const kBazId = std::string{"27b32561185c2825150893774953906c6daa6798"};

auto const kRootSymCommit =
    std::string{"3ecce3f5b19ad7941c6354d65d841590662f33ef"};
auto const kRootSymId = std::string{"18770dacfe14c15d88450c21c16668e13ab0e7f9"};
auto const kBazSymId = std::string{"1868f82682c290f0b1db3cacd092727eef1fa57f"};

TEST_CASE("Serve service client: tree-of-commit request", "[serve_api]") {
    auto const& info = RemoteServeConfig::RemoteAddress();

    // Create TLC client
    SourceTreeClient st_client(info->host, info->port);

    SECTION("Commit in bare checkout") {
        auto root_id = st_client.ServeCommitTree(kRootCommit, ".", false);
        REQUIRE(root_id);
        CHECK(root_id.value() == kRootId);

        auto baz_id = st_client.ServeCommitTree(kRootCommit, "baz", false);
        REQUIRE(baz_id);
        CHECK(baz_id.value() == kBazId);
    }

    SECTION("Commit in non-bare checkout") {
        auto root_id = st_client.ServeCommitTree(kRootSymCommit, ".", false);
        REQUIRE(root_id);
        CHECK(root_id.value() == kRootSymId);

        auto baz_id = st_client.ServeCommitTree(kRootSymCommit, "baz", false);
        REQUIRE(baz_id);
        CHECK(baz_id.value() == kBazSymId);
    }

    SECTION("Subdir not found") {
        auto root_id =
            st_client.ServeCommitTree(kRootCommit, "does_not_exist", false);
        CHECK_FALSE(root_id);
    }

    SECTION("Commit not known") {
        auto root_id = st_client.ServeCommitTree(
            "0123456789abcdef0123456789abcdef01234567", ".", false);
        CHECK_FALSE(root_id);
    }
}
