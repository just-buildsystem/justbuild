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

#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "test/utils/remote_execution/bazel_action_creator.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"

TEST_CASE("Bazel internals: Execution Client", "[execution_api]") {
    std::string instance_name{"remote-execution"};
    std::string content("test");

    HashFunction const hash_function{Compatibility::IsCompatible()
                                         ? HashFunction::Type::PlainSHA256
                                         : HashFunction::Type::GitSHA1};

    auto test_digest = static_cast<bazel_re::Digest>(
        ArtifactDigest::Create<ObjectType::File>(hash_function, content));

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    RetryConfig retry_config{};  // default retry config

    BazelExecutionClient execution_client(remote_config->remote_address->host,
                                          remote_config->remote_address->port,
                                          &*auth_config,
                                          &retry_config);

    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    SECTION("Immediate execution and response") {
        auto action_immediate = CreateAction(instance_name,
                                             {"echo", "-n", content},
                                             {},
                                             remote_config->platform_properties,
                                             hash_function);
        REQUIRE(action_immediate);

        auto response = execution_client.Execute(
            instance_name, *action_immediate, config, true);

        REQUIRE(response.state ==
                BazelExecutionClient::ExecutionResponse::State::Finished);
        REQUIRE(response.output);

        CHECK(response.output->action_result.stdout_digest().hash() ==
              test_digest.hash());
    }

    SECTION("Delayed execution") {
        auto action_delayed =
            CreateAction(instance_name,
                         {"sh", "-c", "sleep 1s; echo -n test"},
                         {},
                         remote_config->platform_properties,
                         hash_function);

        SECTION("Blocking, immediately obtain result") {
            auto response = execution_client.Execute(
                instance_name, *action_delayed, config, true);

            REQUIRE(response.state ==
                    BazelExecutionClient::ExecutionResponse::State::Finished);
            REQUIRE(response.output);

            CHECK(response.output->action_result.stdout_digest().hash() ==
                  test_digest.hash());
        }

        SECTION("Non-blocking, obtain result later") {
            auto response = execution_client.Execute(
                instance_name, *action_delayed, config, false);

            REQUIRE(response.state ==
                    BazelExecutionClient::ExecutionResponse::State::Ongoing);
            response =
                execution_client.WaitExecution(response.execution_handle);
            REQUIRE(response.output);

            CHECK(response.output->action_result.stdout_digest().hash() ==
                  test_digest.hash());
        }
    }
}

TEST_CASE("Bazel internals: Execution Client using env variables",
          "[execution_api]") {
    std::string instance_name{"remote-execution"};
    std::string content("contents of env variable");

    HashFunction const hash_function{Compatibility::IsCompatible()
                                         ? HashFunction::Type::PlainSHA256
                                         : HashFunction::Type::GitSHA1};

    auto test_digest = static_cast<bazel_re::Digest>(
        ArtifactDigest::Create<ObjectType::File>(hash_function, content));

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    RetryConfig retry_config{};  // default retry config

    BazelExecutionClient execution_client(remote_config->remote_address->host,
                                          remote_config->remote_address->port,
                                          &*auth_config,
                                          &retry_config);

    ExecutionConfiguration config;
    config.skip_cache_lookup = false;
    auto action =
        CreateAction(instance_name,
                     {"/bin/sh", "-c", "set -e\necho -n ${MYTESTVAR}"},
                     {{"MYTESTVAR", content}},
                     remote_config->platform_properties,
                     hash_function);
    REQUIRE(action);

    auto response =
        execution_client.Execute(instance_name, *action, config, true);

    REQUIRE(response.state ==
            BazelExecutionClient::ExecutionResponse::State::Finished);
    REQUIRE(response.output);

    CHECK(response.output->action_result.stdout_digest().hash() ==
          test_digest.hash());
}
