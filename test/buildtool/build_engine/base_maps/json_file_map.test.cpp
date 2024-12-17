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

#include "src/buildtool/build_engine/base_maps/json_file_map.hpp"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "test/buildtool/build_engine/base_maps/test_repo.hpp"

namespace {

using namespace BuildMaps::Base;  // NOLINT

auto SetupConfig(std::string target_file_name,
                 bool use_git) -> RepositoryConfig {
    auto root = FileRoot{kBasePath};
    if (use_git) {
        auto repo_path = CreateTestRepo();
        REQUIRE(repo_path);
        auto git_root = FileRoot::FromGit(*repo_path, kJsonTreeId);
        REQUIRE(git_root);
        root = std::move(*git_root);
    }
    auto info = RepositoryConfig::RepositoryInfo{root};
    info.target_file_name = std::move(target_file_name);
    RepositoryConfig repo_config{};
    repo_config.SetInfo("", std::move(info));
    return repo_config;
}

template <bool kMandatory = true>
auto ReadJsonFile(std::string const& target_file_name,
                  ModuleName const& id,
                  JsonFileMap::Consumer value_checker,
                  bool use_git = false,
                  std::optional<JsonFileMap::FailureFunction> fail_func =
                      std::nullopt) -> bool {
    auto repo_config = SetupConfig(target_file_name, use_git);
    auto json_files = CreateJsonFileMap<&RepositoryConfig::WorkspaceRoot,
                                        &RepositoryConfig::TargetFileName,
                                        kMandatory>(&repo_config, 0);
    bool success{true};
    {
        TaskSystem ts;
        json_files.ConsumeAfterKeysReady(
            &ts,
            {id},
            std::move(value_checker),
            [&success](std::string const& /*unused*/, bool /*unused*/) {
                success = false;
            },
            fail_func ? std::move(*fail_func) : [] {});
    }
    return success;
}

}  // namespace

TEST_CASE("simple usage") {
    bool as_expected{false};
    auto name = ModuleName{"", "data_json"};
    auto consumer = [&as_expected](auto values) {
        if ((*values[0])["foo"] == "bar") {
            as_expected = true;
        };
    };

    SECTION("via file") {
        CHECK(ReadJsonFile("foo.json", name, consumer, /*use_git=*/false));
        CHECK(as_expected);
    }

    SECTION("via git tree") {
        CHECK(ReadJsonFile("foo.json", name, consumer, /*use_git=*/true));
        CHECK(as_expected);
    }
}

TEST_CASE("non existent") {
    bool as_expected{false};
    std::atomic<int> failcont_counter{0};

    auto consumer = [&as_expected](auto values) {
        // Missing optional files are expected to result in empty objects with
        // no entries in it.
        if (values[0]->is_object() and values[0]->empty()) {
            as_expected = true;
        };
    };
    auto fail_func = [&failcont_counter]() { ++failcont_counter; };

    SECTION("optional") {
        auto name = ModuleName{"", "missing"};

        SECTION("via file") {
            CHECK(ReadJsonFile<false>(
                "foo.json", name, consumer, /*use_git=*/false, fail_func));
            CHECK(as_expected);
            CHECK(failcont_counter == 0);
        }

        SECTION("via git tree") {
            CHECK(ReadJsonFile<false>(
                "foo.json", name, consumer, /*use_git=*/true, fail_func));
            CHECK(as_expected);
            CHECK(failcont_counter == 0);
        }
    }

    SECTION("mandatory") {
        auto name = ModuleName{"", "missing"};

        SECTION("via file") {
            CHECK_FALSE(ReadJsonFile<true>(
                "foo.json", name, consumer, /*use_git=*/false, fail_func));
            CHECK_FALSE(as_expected);
            CHECK(failcont_counter == 1);
        }

        SECTION("via git tree") {
            CHECK_FALSE(ReadJsonFile<true>(
                "foo.json", name, consumer, /*use_git=*/true, fail_func));
            CHECK_FALSE(as_expected);
            CHECK(failcont_counter == 1);
        }
    }
}

TEST_CASE("Bad syntax") {
    std::atomic<int> failcont_counter{0};
    auto fail_func = [&failcont_counter]() { ++failcont_counter; };
    CHECK_FALSE(ReadJsonFile(
        "bad.json",
        {"", "data_json"},
        [](auto const& /* unused */) {},
        /*use_git=*/false,
        fail_func));
    CHECK(failcont_counter == 1);
}
