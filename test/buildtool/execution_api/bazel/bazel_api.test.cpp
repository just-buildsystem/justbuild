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

#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"

#include <cstdlib>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"

namespace {

class FactoryApi final {
  public:
    explicit FactoryApi(
        gsl::not_null<ServerAddress const*> const& server_address,
        gsl::not_null<Auth const*> const& auth,
        HashFunction hash_function) noexcept
        : address_{*server_address},
          auth_{*auth},
          hash_function_{hash_function} {}

    [[nodiscard]] auto operator()() const -> IExecutionApi::Ptr {
        static RetryConfig retry_config{};  // default retry config
        return IExecutionApi::Ptr{new BazelApi{"remote-execution",
                                               address_.host,
                                               address_.port,
                                               &auth_,
                                               &retry_config,
                                               {},
                                               &hash_function_}};
    }

  private:
    ServerAddress const& address_;
    Auth const& auth_;
    HashFunction const hash_function_;
};

}  // namespace

TEST_CASE("BazelAPI: No input, no output", "[execution_api]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    auto auth = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth);

    FactoryApi api_factory{
        &*remote_config->remote_address, &*auth, hash_function};
    TestNoInputNoOutput(api_factory, remote_config->platform_properties);
}

TEST_CASE("BazelAPI: No input, create output", "[execution_api]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    auto auth = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth);

    FactoryApi api_factory{
        &*remote_config->remote_address, &*auth, hash_function};
    TestNoInputCreateOutput(api_factory, remote_config->platform_properties);
}

TEST_CASE("BazelAPI: One input copied to output", "[execution_api]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    auto auth = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth);

    FactoryApi api_factory{
        &*remote_config->remote_address, &*auth, hash_function};
    TestOneInputCopiedToOutput(api_factory, remote_config->platform_properties);
}

TEST_CASE("BazelAPI: Non-zero exit code, create output", "[execution_api]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    auto auth = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth);

    FactoryApi api_factory{
        &*remote_config->remote_address, &*auth, hash_function};
    TestNonZeroExitCodeCreateOutput(api_factory,
                                    remote_config->platform_properties);
}

TEST_CASE("BazelAPI: Retrieve two identical trees to path", "[execution_api]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    auto auth = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth);

    FactoryApi api_factory{
        &*remote_config->remote_address, &*auth, hash_function};
    TestRetrieveTwoIdenticalTreesToPath(
        api_factory, remote_config->platform_properties, "two_trees");
}

TEST_CASE("BazelAPI: Retrieve file and symlink with same content to path",
          "[execution_api]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    auto auth = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth);

    FactoryApi api_factory{
        &*remote_config->remote_address, &*auth, hash_function};
    TestRetrieveFileAndSymlinkWithSameContentToPath(
        api_factory, remote_config->platform_properties, "file_and_symlink");
}

TEST_CASE("BazelAPI: Retrieve mixed blobs and trees", "[execution_api]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    auto auth = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth);

    FactoryApi api_factory{
        &*remote_config->remote_address, &*auth, hash_function};
    TestRetrieveMixedBlobsAndTrees(
        api_factory, remote_config->platform_properties, "blobs_and_trees");
}

TEST_CASE("BazelAPI: Create directory prior to execution", "[execution_api]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);
    auto auth = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth);

    FactoryApi api_factory{
        &*remote_config->remote_address, &*auth, hash_function};
    TestCreateDirPriorToExecution(api_factory,
                                  remote_config->platform_properties);
}
