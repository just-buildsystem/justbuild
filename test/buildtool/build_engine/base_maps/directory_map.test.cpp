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

#include <filesystem>
#include <memory>
#include <string>
#include <utility>  // std::move

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "test/buildtool/build_engine/base_maps/test_repo.hpp"

namespace {

using namespace BuildMaps::Base;  // NOLINT

auto SetupConfig(bool use_git) -> RepositoryConfig {
    auto root = FileRoot{kBasePath / "data_src"};
    if (use_git) {
        auto repo_path = CreateTestRepo();
        REQUIRE(repo_path);
        auto git_root = FileRoot::FromGit(*repo_path, kSrcTreeId);
        REQUIRE(git_root);
        root = std::move(*git_root);
    }
    RepositoryConfig repo_config{};
    repo_config.SetInfo("", RepositoryConfig::RepositoryInfo{root});
    return repo_config;
}

auto ReadDirectory(ModuleName const& id,
                   DirectoryEntriesMap::Consumer value_checker,
                   bool use_git = false) -> bool {
    auto repo_config = SetupConfig(use_git);
    auto data_direntries = CreateDirectoryEntriesMap(&repo_config);
    bool success{true};
    {
        TaskSystem ts;
        data_direntries.ConsumeAfterKeysReady(
            &ts,
            {id},
            std::move(value_checker),
            [&success](std::string const& /*unused*/, bool /*unused*/) {
                success = false;
            });
    }
    return success;
}

}  // namespace

TEST_CASE("simple usage") {
    bool as_expected{false};
    auto name = ModuleName{"", "."};
    auto consumer = [&as_expected](auto values) {
        if (values[0]->ContainsBlob("file") and
            not values[0]->ContainsBlob("does_not_exist")) {
            as_expected = true;
        };
    };

    SECTION("via file") {
        CHECK(ReadDirectory(name, consumer, /*use_git=*/false));
        CHECK(as_expected);
    }

    SECTION("via git tree") {
        CHECK(ReadDirectory(name, consumer, /*use_git=*/true));
        CHECK(as_expected);
    }
}

TEST_CASE("missing directory") {
    bool as_expected{false};
    auto name = ModuleName{"", "does_not_exist"};
    auto consumer = [&as_expected](auto values) {
        if (values[0]->Empty()) {
            as_expected = true;
        }
    };

    SECTION("via file") {
        CHECK(ReadDirectory(name, consumer, /*use_git=*/false));
        CHECK(as_expected);
    }

    SECTION("via git tree") {
        CHECK(ReadDirectory(name, consumer, /*use_git=*/true));
        CHECK(as_expected);
    }
}
