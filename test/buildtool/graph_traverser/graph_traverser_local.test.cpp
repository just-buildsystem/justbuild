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
#include <variant>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/buildtool/graph_traverser/graph_traverser.test.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

TEST_CASE("Local: Output created when entry point is local artifact",
          "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestCopyLocalFile(storage_config.Get(), storage, &auth, &remote_config);
}

TEST_CASE("Local: Output created and contents are correct",
          "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestHelloWorldCopyMessage(
        storage_config.Get(), storage, &auth, &remote_config);
}

TEST_CASE("Local: Actions are not re-run", "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestSequencePrinterBuildLibraryOnly(
        storage_config.Get(), storage, &auth, &remote_config);
}

TEST_CASE("Local: KNOWN artifact", "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestHelloWorldWithKnownSource(
        storage_config.Get(), storage, &auth, &remote_config);
}

TEST_CASE("Local: Blobs uploaded and correctly used", "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestBlobsUploadedAndUsed(
        storage_config.Get(), storage, &auth, &remote_config);
}

TEST_CASE("Local: Environment variables are set and used",
          "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestEnvironmentVariablesSetAndUsed(
        storage_config.Get(), storage, &auth, &remote_config);
}

TEST_CASE("Local: Trees correctly used", "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestTreesUsed(storage_config.Get(), storage, &auth, &remote_config);
}

TEST_CASE("Local: Nested trees correctly used", "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestNestedTreesUsed(storage_config.Get(), storage, &auth, &remote_config);
}

TEST_CASE("Local: Detect flaky actions", "[graph_traverser]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    Auth auth{};                           /*no TLS needed*/
    RemoteExecutionConfig remote_config{}; /*no remote*/

    TestFlakyHelloWorldDetected(
        storage_config.Get(), storage, &auth, &remote_config);
}
