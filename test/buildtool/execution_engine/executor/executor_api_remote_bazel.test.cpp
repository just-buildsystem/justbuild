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

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "test/buildtool/execution_engine/executor/executor_api.test.hpp"

TEST_CASE("Executor<BazelApi>: Upload blob", "[executor]") {
    ExecutionConfiguration config;
    auto const& info = RemoteExecutionConfig::RemoteAddress();

    TestBlobUpload([&] {
        return BazelApi::Ptr{
            new BazelApi{"remote-execution", info->host, info->port, config}};
    });
}

TEST_CASE("Executor<BazelApi>: Compile hello world", "[executor]") {
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto const& info = RemoteExecutionConfig::RemoteAddress();

    TestHelloWorldCompilation(
        [&] {
            return BazelApi::Ptr{new BazelApi{
                "remote-execution", info->host, info->port, config}};
        },
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Compile greeter", "[executor]") {
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto const& info = RemoteExecutionConfig::RemoteAddress();

    TestGreeterCompilation(
        [&] {
            return BazelApi::Ptr{new BazelApi{
                "remote-execution", info->host, info->port, config}};
        },
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Upload and download trees", "[executor]") {
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto const& info = RemoteExecutionConfig::RemoteAddress();

    TestUploadAndDownloadTrees(
        [&] {
            return BazelApi::Ptr{new BazelApi{
                "remote-execution", info->host, info->port, config}};
        },
        false /* not hermetic */);
}

TEST_CASE("Executor<BazelApi>: Retrieve output directories", "[executor]") {
    ExecutionConfiguration config;
    config.skip_cache_lookup = false;

    auto const& info = RemoteExecutionConfig::RemoteAddress();

    TestRetrieveOutputDirectories(
        [&] {
            return BazelApi::Ptr{new BazelApi{
                "remote-execution", info->host, info->port, config}};
        },
        false /* not hermetic */);
}
