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

#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "test/utils/remote_execution/bazel_action_creator.hpp"
#include "test/utils/test_env.hpp"

TEST_CASE("Bazel internals: Execution Client", "[execution_api]") {
    auto const& info = RemoteExecutionConfig::RemoteAddress();

    std::string instance_name{"remote-execution"};
    std::string content("test");
    auto test_digest = static_cast<bazel_re::Digest>(
        ArtifactDigest::Create<ObjectType::File>(content));

    BazelExecutionClient execution_client(info->host, info->port);

    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    SECTION("Immediate execution and response") {
        auto action_immediate =
            CreateAction(instance_name,
                         {"echo", "-n", content},
                         {},
                         RemoteExecutionConfig::PlatformProperties());
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
                         RemoteExecutionConfig::PlatformProperties());

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
    auto const& info = RemoteExecutionConfig::RemoteAddress();

    std::string instance_name{"remote-execution"};
    std::string content("contents of env variable");
    auto test_digest = static_cast<bazel_re::Digest>(
        ArtifactDigest::Create<ObjectType::File>(content));

    BazelExecutionClient execution_client(info->host, info->port);

    ExecutionConfiguration config;
    config.skip_cache_lookup = false;
    auto action =
        CreateAction(instance_name,
                     {"/bin/sh", "-c", "set -e\necho -n ${MYTESTVAR}"},
                     {{"MYTESTVAR", content}},
                     RemoteExecutionConfig::PlatformProperties());
    REQUIRE(action);

    auto response =
        execution_client.Execute(instance_name, *action, config, true);

    REQUIRE(response.state ==
            BazelExecutionClient::ExecutionResponse::State::Finished);
    REQUIRE(response.output);

    CHECK(response.output->action_result.stdout_digest().hash() ==
          test_digest.hash());
}
