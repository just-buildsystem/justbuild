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

#include "src/buildtool/serve_api/remote/source_tree_client.hpp"

#include <string>
#include <variant>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "test/utils/serve_service/test_serve_config.hpp"

auto const kRootCommit =
    std::string{"e4fc610c60716286b98cf51ad0c8f0d50f3aebb5"};
auto const kRootId = std::string{"c610db170fbcad5f2d66fe19972495923f3b2536"};
auto const kBazId = std::string{"27b32561185c2825150893774953906c6daa6798"};

auto const kRootSymCommit =
    std::string{"3ecce3f5b19ad7941c6354d65d841590662f33ef"};
auto const kRootSymId = std::string{"18770dacfe14c15d88450c21c16668e13ab0e7f9"};
auto const kBazSymId = std::string{"1868f82682c290f0b1db3cacd092727eef1fa57f"};

TEST_CASE("Serve service client: tree-of-commit request", "[serve_api]") {
    auto config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(config);
    REQUIRE(config->remote_address);

    // Create TLC client
    Auth auth{};
    RetryConfig retry_config{};
    RemoteExecutionConfig exec_config{};
    RemoteContext const remote_context{.auth = &auth,
                                       .retry_config = &retry_config,
                                       .exec_config = &exec_config};

    SourceTreeClient st_client(*config->remote_address, &remote_context);

    SECTION("Commit in bare checkout") {
        auto root_id = st_client.ServeCommitTree(kRootCommit, ".", false);
        REQUIRE(root_id);
        CHECK(*root_id == kRootId);

        auto baz_id = st_client.ServeCommitTree(kRootCommit, "baz", false);
        REQUIRE(baz_id);
        CHECK(*baz_id == kBazId);
    }

    SECTION("Commit in non-bare checkout") {
        auto root_id = st_client.ServeCommitTree(kRootSymCommit, ".", false);
        REQUIRE(root_id);
        CHECK(*root_id == kRootSymId);

        auto baz_id = st_client.ServeCommitTree(kRootSymCommit, "baz", false);
        REQUIRE(baz_id);
        CHECK(*baz_id == kBazSymId);
    }

    SECTION("Subdir not found") {
        auto root_id =
            st_client.ServeCommitTree(kRootCommit, "does_not_exist", false);
        REQUIRE_FALSE(root_id);
        CHECK(root_id.error() == GitLookupError::Fatal);  // fatal failure
    }

    SECTION("Commit not known") {
        auto root_id = st_client.ServeCommitTree(
            "0123456789abcdef0123456789abcdef01234567", ".", false);
        REQUIRE_FALSE(root_id);
        CHECK_FALSE(root_id.error() ==
                    GitLookupError::Fatal);  // non-fatal failure
    }
}
