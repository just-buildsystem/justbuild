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

#include <optional>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "test/buildtool/execution_engine/executor/executor_api.test.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"

TEST_CASE("Executor<BazelApi>: Upload blob", "[executor]") {
    RepositoryConfig repo_config{};
    ExecutionConfiguration config;

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    RetryConfig retry_config{};  // default retry config

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    TestBlobUpload(&repo_config, [&] {
        return BazelApi::Ptr{new BazelApi{"remote-execution",
                                          remote_config->remote_address->host,
                                          remote_config->remote_address->port,
                                          &*auth_config,
                                          &retry_config,
                                          config,
                                          &hash_function}};
    });
}

TEST_CASE("Executor<BazelApi>: Compile hello world", "[executor]") {
    RepositoryConfig repo_config{};
    Statistics stats{};
    Progress progress{};
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    RetryConfig retry_config{};  // default retry config

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    TestHelloWorldCompilation(
        &repo_config,
        &stats,
        &progress,
        [&] {
            return BazelApi::Ptr{
                new BazelApi{"remote-execution",
                             remote_config->remote_address->host,
                             remote_config->remote_address->port,
                             &*auth_config,
                             &retry_config,
                             config,
                             &hash_function}};
        },
        &*auth_config,
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Compile greeter", "[executor]") {
    RepositoryConfig repo_config{};
    Statistics stats{};
    Progress progress{};
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    RetryConfig retry_config{};  // default retry config

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    TestGreeterCompilation(
        &repo_config,
        &stats,
        &progress,
        [&] {
            return BazelApi::Ptr{
                new BazelApi{"remote-execution",
                             remote_config->remote_address->host,
                             remote_config->remote_address->port,
                             &*auth_config,
                             &retry_config,
                             config,
                             &hash_function}};
        },
        &*auth_config,
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Upload and download trees", "[executor]") {
    RepositoryConfig repo_config{};
    Statistics stats{};
    Progress progress{};
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    RetryConfig retry_config{};  // default retry config

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    TestUploadAndDownloadTrees(
        &repo_config,
        &stats,
        &progress,
        [&] {
            return BazelApi::Ptr{
                new BazelApi{"remote-execution",
                             remote_config->remote_address->host,
                             remote_config->remote_address->port,
                             &*auth_config,
                             &retry_config,
                             config,
                             &hash_function}};
        },
        &*auth_config,
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Retrieve output directories", "[executor]") {
    RepositoryConfig repo_config{};
    Statistics stats{};
    Progress progress{};
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    RetryConfig retry_config{};  // default retry config

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    TestRetrieveOutputDirectories(
        &repo_config,
        &stats,
        &progress,
        [&] {
            return BazelApi::Ptr{
                new BazelApi{"remote-execution",
                             remote_config->remote_address->host,
                             remote_config->remote_address->port,
                             &*auth_config,
                             &retry_config,
                             config,
                             &hash_function}};
        },
        &*auth_config,
        false /* not hermetic */);
}
