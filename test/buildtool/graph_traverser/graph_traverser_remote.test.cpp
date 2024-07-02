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
#include "test/buildtool/graph_traverser/graph_traverser.test.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"

TEST_CASE("Remote: Output created and contents are correct",
          "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestHelloWorldCopyMessage(&*auth_config, false /* not hermetic */);
}

TEST_CASE("Remote: Output created when entry point is local artifact",
          "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestCopyLocalFile(&*auth_config, false /* not hermetic */);
}

TEST_CASE("Remote: Actions are not re-run", "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestSequencePrinterBuildLibraryOnly(&*auth_config,
                                        false /* not hermetic */);
}

TEST_CASE("Remote: KNOWN artifact", "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestHelloWorldWithKnownSource(&*auth_config, false /* not hermetic */);
}

TEST_CASE("Remote: Blobs uploaded and correctly used", "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestBlobsUploadedAndUsed(&*auth_config, false /* not hermetic */);
}

TEST_CASE("Remote: Environment variables are set and used",
          "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestEnvironmentVariablesSetAndUsed(&*auth_config, false /* not hermetic */);
}

TEST_CASE("Remote: Trees correctly used", "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestTreesUsed(&*auth_config, false /* not hermetic */);
}

TEST_CASE("Remote: Nested trees correctly used", "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestNestedTreesUsed(&*auth_config, false /* not hermetic */);
}

TEST_CASE("Remote: Detect flaky actions", "[graph_traverser]") {
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);

    TestFlakyHelloWorldDetected(&*auth_config, false /* not hermetic */);
}
