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

#include "src/buildtool/build_engine/base_maps/source_map.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/config.hpp"
#include "test/buildtool/build_engine/base_maps/test_repo.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

namespace {

using namespace BuildMaps::Base;  // NOLINT

auto SetupConfig(StorageConfig const* storage_config,
                 bool use_git) -> RepositoryConfig {
    // manually create locally a test symlink in data_src; should match the
    // git test_repo structure
    if (not use_git) {
        auto link_path = kBasePath / "data_src/foo/link";
        if (not FileSystemManager::Exists(link_path)) {
            REQUIRE(FileSystemManager::CreateSymlink("dummy", link_path));
        }
    }
    auto root = FileRoot{kBasePath / "data_src"};
    if (use_git) {
        auto repo_path = CreateTestRepo();
        REQUIRE(repo_path);
        REQUIRE(storage_config);
        auto git_root =
            FileRoot::FromGit(storage_config, *repo_path, kSrcTreeId);
        REQUIRE(git_root);
        root = std::move(*git_root);
    }
    RepositoryConfig repo_config{};
    repo_config.SetInfo("", RepositoryConfig::RepositoryInfo{root});
    return repo_config;
}

auto ReadSourceTarget(EntityName const& id,
                      SourceTargetMap::Consumer consumer,
                      HashFunction::Type hash_type,
                      StorageConfig const* storage_config,
                      bool use_git = false,
                      std::optional<SourceTargetMap::FailureFunction>
                          fail_func = std::nullopt) -> bool {
    auto repo_config = SetupConfig(storage_config, use_git);
    auto directory_entries = CreateDirectoryEntriesMap(&repo_config);
    auto source_artifacts =
        CreateSourceTargetMap(&directory_entries, &repo_config, hash_type);
    std::string error_msg;
    bool success{true};
    {
        TaskSystem ts;
        source_artifacts.ConsumeAfterKeysReady(
            &ts,
            {id},
            std::move(consumer),
            [&success, &error_msg](std::string const& msg, bool /*unused*/) {
                success = false;
                error_msg = msg;
            },
            fail_func ? std::move(*fail_func) : [] {});
    }
    return success and error_msg.empty();
}

}  // namespace

TEST_CASE("from file") {
    auto const hash_type = TestHashType::ReadFromEnvironment();

    nlohmann::json artifacts;
    auto name = EntityName{"", ".", "file"};
    auto consumer = [&artifacts](auto values) {
        artifacts = (*values[0])->Artifacts()->ToJson();
    };

    SECTION("via file") {
        CHECK(ReadSourceTarget(
            name, consumer, hash_type, nullptr, /*use_git=*/false));
        CHECK(artifacts["file"]["type"] == "LOCAL");
        CHECK(artifacts["file"]["data"]["path"] == "file");
    }

    SECTION("via git tree") {
        auto const storage_config = TestStorageConfig::Create();
        CHECK(ReadSourceTarget(name,
                               consumer,
                               hash_type,
                               &storage_config.Get(),
                               /*use_git=*/true));
        CHECK(artifacts["file"]["type"] == "KNOWN");
        CHECK(
            artifacts["file"]["data"]["id"] ==
            (ProtocolTraits::IsNative(hash_type) ? kEmptySha1 : kEmptySha256));
        CHECK(artifacts["file"]["data"]["size"] == 0);
    }
}

TEST_CASE("not present at all") {
    auto const hash_type = TestHashType::ReadFromEnvironment();

    bool consumed{false};
    bool failure_called{false};
    auto name = EntityName{"", ".", "does_not_exist"};
    auto consumer = [&consumed](auto /*unused*/) { consumed = true; };
    auto fail_func = [&failure_called]() { failure_called = true; };

    SECTION("via file") {
        CHECK_FALSE(ReadSourceTarget(
            name, consumer, hash_type, nullptr, /*use_git=*/false, fail_func));
        CHECK_FALSE(consumed);
        CHECK(failure_called);
    }

    SECTION("via git tree") {
        auto const storage_config = TestStorageConfig::Create();
        CHECK_FALSE(ReadSourceTarget(name,
                                     consumer,
                                     hash_type,
                                     &storage_config.Get(),
                                     /*use_git=*/true,
                                     fail_func));
        CHECK_FALSE(consumed);
        CHECK(failure_called);
    }
}

TEST_CASE("malformed entry") {
    auto const hash_type = TestHashType::ReadFromEnvironment();

    bool consumed{false};
    bool failure_called{false};
    auto name = EntityName{"", ".", "bad_entry"};
    auto consumer = [&consumed](auto /*unused*/) { consumed = true; };
    auto fail_func = [&failure_called]() { failure_called = true; };

    SECTION("via git tree") {
        CHECK_FALSE(ReadSourceTarget(
            name, consumer, hash_type, nullptr, /*use_git=*/false, fail_func));
        CHECK_FALSE(consumed);
        CHECK(failure_called);
    }

    SECTION("via git tree") {
        auto const storage_config = TestStorageConfig::Create();
        CHECK_FALSE(ReadSourceTarget(name,
                                     consumer,
                                     hash_type,
                                     &storage_config.Get(),
                                     /*use_git=*/true,
                                     fail_func));
        CHECK_FALSE(consumed);
        CHECK(failure_called);
    }
}

TEST_CASE("subdir file") {
    auto const hash_type = TestHashType::ReadFromEnvironment();

    nlohmann::json artifacts;
    auto name = EntityName{"", "foo", "bar/file"};
    auto consumer = [&artifacts](auto values) {
        artifacts = (*values[0])->Artifacts()->ToJson();
    };

    SECTION("via file") {
        CHECK(ReadSourceTarget(
            name, consumer, hash_type, nullptr, /*use_git=*/false));
        CHECK(artifacts["bar/file"]["type"] == "LOCAL");
        CHECK(artifacts["bar/file"]["data"]["path"] == "foo/bar/file");
    }

    SECTION("via git tree") {
        auto const storage_config = TestStorageConfig::Create();
        CHECK(ReadSourceTarget(name,
                               consumer,
                               hash_type,
                               &storage_config.Get(),
                               /*use_git=*/true));
        CHECK(artifacts["bar/file"]["type"] == "KNOWN");
        CHECK(
            artifacts["bar/file"]["data"]["id"] ==
            (ProtocolTraits::IsNative(hash_type) ? kEmptySha1 : kEmptySha256));
        CHECK(artifacts["bar/file"]["data"]["size"] == 0);
    }
}

TEST_CASE("subdir symlink") {
    auto const hash_type = TestHashType::ReadFromEnvironment();

    nlohmann::json artifacts;
    auto name = EntityName{"", "foo", "link"};
    auto consumer = [&artifacts](auto values) {
        artifacts = (*values[0])->Artifacts()->ToJson();
    };

    SECTION("via file") {
        CHECK(ReadSourceTarget(
            name, consumer, hash_type, nullptr, /*use_git=*/false));
        CHECK(artifacts["link"]["type"] == "LOCAL");
        CHECK(artifacts["link"]["data"]["path"] == "foo/link");
    }

    SECTION("via git tree") {
        auto const storage_config = TestStorageConfig::Create();
        CHECK(ReadSourceTarget(name,
                               consumer,
                               hash_type,
                               &storage_config.Get(),
                               /*use_git=*/true));
        CHECK(artifacts["link"]["type"] == "KNOWN");
        CHECK(artifacts["link"]["data"]["id"] ==
              (ProtocolTraits::IsNative(hash_type) ? kSrcLinkIdSha1
                                                   : kSrcLinkIdSha256));
        CHECK(artifacts["link"]["data"]["size"] == 5);  // content: dummy
    }
}
