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
#include "test/utils/hermeticity/local.hpp"

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Output created when entry point is local artifact",
                 "[graph_traverser]") {
    TestCopyLocalFile(/*auth=*/nullptr);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Output created and contents are correct",
                 "[graph_traverser]") {
    TestHelloWorldCopyMessage(/*auth=*/nullptr);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Actions are not re-run",
                 "[graph_traverser]") {
    TestSequencePrinterBuildLibraryOnly(/*auth=*/nullptr);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: KNOWN artifact",
                 "[graph_traverser]") {
    TestHelloWorldWithKnownSource(/*auth=*/nullptr);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Blobs uploaded and correctly used",
                 "[graph_traverser]") {
    TestBlobsUploadedAndUsed(/*auth=*/nullptr);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Environment variables are set and used",
                 "[graph_traverser]") {
    TestEnvironmentVariablesSetAndUsed(/*auth=*/nullptr);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Trees correctly used",
                 "[graph_traverser]") {
    TestTreesUsed(/*auth=*/nullptr);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Nested trees correctly used",
                 "[graph_traverser]") {
    TestNestedTreesUsed(/*auth=*/nullptr);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Detect flaky actions",
                 "[graph_traverser]") {
    TestFlakyHelloWorldDetected(/*auth=*/nullptr);
}
