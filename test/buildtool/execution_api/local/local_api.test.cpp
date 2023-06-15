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
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/hermeticity/local.hpp"

namespace {

auto const kApiFactory = []() { return IExecutionApi::Ptr{new LocalApi()}; };

}  // namespace

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: No input, no output",
                 "[execution_api]") {
    TestNoInputNoOutput(kApiFactory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: No input, create output",
                 "[execution_api]") {
    TestNoInputCreateOutput(kApiFactory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: One input copied to output",
                 "[execution_api]") {
    TestOneInputCopiedToOutput(kApiFactory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Non-zero exit code, create output",
                 "[execution_api]") {
    TestNonZeroExitCodeCreateOutput(kApiFactory, {});
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Retrieve two identical trees to path",
                 "[execution_api]") {
    TestRetrieveTwoIdenticalTreesToPath(
        kApiFactory, {}, "two_trees", /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Retrieve file and symlink with same content to "
                 "path",
                 "[execution_api]") {
    TestRetrieveFileAndSymlinkWithSameContentToPath(
        kApiFactory, {}, "file_and_symlink", /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Retrieve mixed blobs and trees",
                 "[execution_api]") {
    TestRetrieveMixedBlobsAndTrees(
        kApiFactory, {}, "blobs_and_trees", /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Create directory prior to execution",
                 "[execution_api]") {
    TestCreateDirPriorToExecution(kApiFactory, {}, /*is_hermetic=*/true);
}
