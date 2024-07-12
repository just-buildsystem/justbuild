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
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/buildtool/graph_traverser/graph_traverser.test.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"

[[nodiscard]] static auto CreateStorageConfig(
    RemoteExecutionConfig const& remote_config) -> StorageConfig {
    auto cache_dir = FileSystemManager::GetCurrentDirectory() / "cache";
    if (not FileSystemManager::RemoveDirectory(cache_dir, true) or
        not FileSystemManager::CreateDirectoryExclusive(cache_dir)) {
        Logger::Log(LogLevel::Error,
                    "failed to create a test-local cache dir {}",
                    cache_dir.string());
        std::exit(EXIT_FAILURE);
    }

    StorageConfig::Builder builder;
    auto config = builder.SetBuildRoot(cache_dir)
                      .SetRemoteExecutionArgs(remote_config.remote_address,
                                              remote_config.platform_properties,
                                              remote_config.dispatch)
                      .Build();
    if (not config) {
        Logger::Log(LogLevel::Error, config.error());
        std::exit(EXIT_FAILURE);
    }
    return *std::move(config);
}

TEST_CASE("Remote: Output created and contents are correct",
          "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestHelloWorldCopyMessage(storage_config,
                              storage,
                              &*auth_config,
                              &*remote_config,
                              false /* not hermetic */);
}

TEST_CASE("Remote: Output created when entry point is local artifact",
          "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestCopyLocalFile(storage_config,
                      storage,
                      &*auth_config,
                      &*remote_config,
                      false /* not hermetic */);
}

TEST_CASE("Remote: Actions are not re-run", "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestSequencePrinterBuildLibraryOnly(storage_config,
                                        storage,
                                        &*auth_config,
                                        &*remote_config,
                                        false /* not hermetic */);
}

TEST_CASE("Remote: KNOWN artifact", "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestHelloWorldWithKnownSource(storage_config,
                                  storage,
                                  &*auth_config,
                                  &*remote_config,
                                  false /* not hermetic */);
}

TEST_CASE("Remote: Blobs uploaded and correctly used", "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestBlobsUploadedAndUsed(storage_config,
                             storage,
                             &*auth_config,
                             &*remote_config,
                             false /* not hermetic */);
}

TEST_CASE("Remote: Environment variables are set and used",
          "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestEnvironmentVariablesSetAndUsed(storage_config,
                                       storage,
                                       &*auth_config,
                                       &*remote_config,
                                       false /* not hermetic */);
}

TEST_CASE("Remote: Trees correctly used", "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestTreesUsed(storage_config,
                  storage,
                  &*auth_config,
                  &*remote_config,
                  false /* not hermetic */);
}

TEST_CASE("Remote: Nested trees correctly used", "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestNestedTreesUsed(storage_config,
                        storage,
                        &*auth_config,
                        &*remote_config,
                        false /* not hermetic */);
}

TEST_CASE("Remote: Detect flaky actions", "[graph_traverser]") {
    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);

    StorageConfig const storage_config = CreateStorageConfig(*remote_config);
    auto const storage = Storage::Create(&storage_config);
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    TestFlakyHelloWorldDetected(storage_config,
                                storage,
                                &*auth_config,
                                &*remote_config,
                                false /* not hermetic */);
}
