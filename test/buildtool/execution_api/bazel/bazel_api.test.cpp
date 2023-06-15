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

#include <cstdlib>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/test_env.hpp"

namespace {

auto const kApiFactory = []() {
    static auto const& server = RemoteExecutionConfig::RemoteAddress();
    return IExecutionApi::Ptr{
        new BazelApi{"remote-execution", server->host, server->port, {}}};
};

}  // namespace

TEST_CASE("BazelAPI: No input, no output", "[execution_api]") {
    TestNoInputNoOutput(kApiFactory,
                        RemoteExecutionConfig::PlatformProperties());
}

TEST_CASE("BazelAPI: No input, create output", "[execution_api]") {
    TestNoInputCreateOutput(kApiFactory,
                            RemoteExecutionConfig::PlatformProperties());
}

TEST_CASE("BazelAPI: One input copied to output", "[execution_api]") {
    TestOneInputCopiedToOutput(kApiFactory,
                               RemoteExecutionConfig::PlatformProperties());
}

TEST_CASE("BazelAPI: Non-zero exit code, create output", "[execution_api]") {
    TestNonZeroExitCodeCreateOutput(
        kApiFactory, RemoteExecutionConfig::PlatformProperties());
}

TEST_CASE("BazelAPI: Retrieve two identical trees to path", "[execution_api]") {
    TestRetrieveTwoIdenticalTreesToPath(
        kApiFactory, RemoteExecutionConfig::PlatformProperties(), "two_trees");
}

TEST_CASE("BazelAPI: Retrieve file and symlink with same content to path",
          "[execution_api]") {
    TestRetrieveFileAndSymlinkWithSameContentToPath(
        kApiFactory,
        RemoteExecutionConfig::PlatformProperties(),
        "file_and_symlink");
}

TEST_CASE("BazelAPI: Retrieve mixed blobs and trees", "[execution_api]") {
    TestRetrieveMixedBlobsAndTrees(kApiFactory,
                                   RemoteExecutionConfig::PlatformProperties(),
                                   "blobs_and_trees");
}

TEST_CASE("BazelAPI: Create directory prior to execution", "[execution_api]") {
    TestCreateDirPriorToExecution(kApiFactory,
                                  RemoteExecutionConfig::PlatformProperties());
}
