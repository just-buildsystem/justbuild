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
#include <string>
#include <utility>  // std::move

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/expression_map.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/base_maps/source_map.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/main/analyse_context.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"
#include "test/utils/serve_service/test_serve_config.hpp"

namespace {

using none_t = Expression::none_t;

auto CreateSymlinks() -> bool {
    auto base_src = std::filesystem::path(
        "test/buildtool/build_engine/target_map/data_src");
    // create the symlinks
    REQUIRE(FileSystemManager::CreateSymlink(
        "dummy", base_src / "a" / "b" / "targets_here" / "c" / "d" / "link"));
    REQUIRE(FileSystemManager::CreateSymlink(
        "dummy", base_src / "symlink_reference" / "link"));
    REQUIRE(FileSystemManager::CreateSymlink(
        "dummy", base_src / "simple_targets" / "link"));

    return true;
}

auto SetupConfig() -> RepositoryConfig {
    // manually create locally test symlinks in data_src, but only once
    [[maybe_unused]] static auto done = CreateSymlinks();
    // create the file roots
    auto info = RepositoryConfig::RepositoryInfo{
        FileRoot{std::filesystem::path{"test/buildtool/build_engine/target_map/"
                                       "data_src"}},
        FileRoot{std::filesystem::path{"test/buildtool/build_engine/target_map/"
                                       "data_targets"}},
        FileRoot{std::filesystem::path{"test/buildtool/build_engine/target_map/"
                                       "data_rules"}},
        FileRoot{std::filesystem::path{"test/buildtool/build_engine/target_map/"
                                       "data_expr"}}};
    RepositoryConfig repo_config{};
    repo_config.SetInfo("", std::move(info));
    return repo_config;
}

}  // namespace

TEST_CASE("simple targets", "[target_map]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());

    auto repo_config = SetupConfig();
    auto directory_entries =
        BuildMaps::Base::CreateDirectoryEntriesMap(&repo_config);
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries,
                                                         &repo_config);
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(&repo_config, 0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(&repo_config, 0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(&repo_config, 0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map,
                                                         &repo_config);
    auto rule_map =
        BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map, &repo_config);
    BuildMaps::Target::ResultTargetMap result_map{0};
    Statistics stats{};
    Progress exports_progress{};

    auto serve_config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(serve_config);

    Auth auth{};
    ApiBundle const apis{&storage_config.Get(),
                         &storage,
                         /*repo_config=*/nullptr,
                         &auth,
                         RemoteExecutionConfig::RemoteAddress()};
    auto serve = ServeApi::Create(*serve_config, &storage, &apis);
    AnalyseContext ctx{.repo_config = &repo_config,
                       .storage = &storage,
                       .statistics = &stats,
                       .progress = &exports_progress,
                       .serve = serve ? &*serve : nullptr};

    auto absent_target_variables_map =
        BuildMaps::Target::CreateAbsentTargetVariablesMap(&ctx, 0);

    auto absent_target_map = BuildMaps::Target::CreateAbsentTargetMap(
        &ctx, &result_map, &absent_target_variables_map, 0);

    auto target_map = BuildMaps::Target::CreateTargetMap(&ctx,
                                                         &source,
                                                         &targets_file_map,
                                                         &rule_map,
                                                         &directory_entries,
                                                         &absent_target_map,
                                                         &result_map);

    AnalysedTargetPtr result;
    bool error{false};
    std::string error_msg;
    auto empty_config = Configuration{Expression::FromJson(R"({})"_json)};

    SECTION("Actual source file") {
        {
            error_msg = "NONE";
            error = false;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "a/b/targets_here", "c/d/foo"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        auto artifacts = result->Artifacts();
        ExpressionPtr artifact = artifacts->Get("c/d/foo", none_t{});
        CHECK(artifact->IsArtifact());
    }

    SECTION("Actual source symlink file") {
        {
            error_msg = "NONE";
            error = false;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "a/b/targets_here", "c/d/link"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        auto artifacts = result->Artifacts();
        ExpressionPtr artifact = artifacts->Get("c/d/link", none_t{});
        CHECK(artifact->IsArtifact());
    }

    SECTION("No targets file here") {
        {
            error_msg = "NONE";
            error = false;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "a/b/targets_here/c", "d/foo"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
    }

    SECTION("Rule just provides") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "rule just provides"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        CHECK(result->Provides() ==
              Expression::FromJson(R"({"foo": "bar"})"_json));
    }

    SECTION("Rule provides variable, but unset") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "rule provides FOO"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        CHECK(result->Provides() ==
              Expression::FromJson(R"({"foo": null})"_json));
    }

    SECTION("Rule provides variable, set in config") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;
            auto config = Configuration{
                Expression::FromJson(R"({"FOO": "foobar"})"_json)};

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "rule provides FOO"},
                    .config = config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        CHECK(result->Provides() ==
              Expression::FromJson(R"({"foo": "foobar"})"_json));
    }

    SECTION("Rule provides variable, set via config transition") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;
            auto config = Configuration{
                Expression::FromJson(R"({"FOO": "foobar"})"_json)};

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "config transition for FOO"},
                    .config = config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        CHECK(
            result->Provides() ==
            Expression::FromJson(R"({"transitioned deps": ["barbaz"]})"_json));
    }

    SECTION("Rule collects dependency artifacts") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "collect dep artifacts"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        // Look into the internals of the artifacts by using the json
        // representation
        auto artifacts_desc = result->Artifacts()->ToJson();
        CHECK(artifacts_desc["foo.txt"]["data"]["path"] ==
              "simple_targets/foo.txt");
        CHECK(artifacts_desc["bar.txt"]["data"]["path"] ==
              "simple_targets/bar.txt");
        CHECK(artifacts_desc["baz.txt"]["data"]["path"] ==
              "simple_targets/baz.txt");
        CHECK(artifacts_desc["link"]["data"]["path"] == "simple_targets/link");
    }

    SECTION("Rule stages blob") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "stage blob"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        auto blobs = result->Blobs();
        CHECK(blobs.size() == 1);
        CHECK(blobs[0] == "This is FOO!");
        auto artifacts_desc = result->Artifacts()->ToJson();
        CHECK(artifacts_desc["foo.txt"]["type"] == "KNOWN");
    }

    SECTION("Rule stages symlink") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "stage link"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        auto blobs = result->Blobs();
        CHECK(blobs.size() == 1);
        CHECK(blobs[0] == "this/is/a/link");
        auto artifacts_desc = result->Artifacts()->ToJson();
        CHECK(artifacts_desc["foo.txt"]["type"] == "KNOWN");
        CHECK(artifacts_desc["foo.txt"]["data"]["file_type"] == "l");
    }

    SECTION("Rule stages symlink, bad: absolute") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "bad absolute link"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
    }

    SECTION("Rule stages symlink, bad: upwards") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "bad upwards link"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
    }

    SECTION("Stage implicit target") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "use implicit"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        // Look into the internals of the artifacts by using the json
        // representation
        auto artifacts_desc = result->Artifacts()->ToJson();
        CHECK(artifacts_desc["implicit_script.sh"]["data"]["path"] ==
              "simple_rules/implicit_script.sh");
    }

    SECTION("simple actions") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "actions"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(not error);
        CHECK(error_msg == "NONE");
        // Look into the internals of the artifacts by using the json
        // representation
        auto artifacts_desc = result->Artifacts()->ToJson();
        CHECK(artifacts_desc["foo.txt"]["type"] == "ACTION");
        CHECK(artifacts_desc["bar.txt"]["type"] == "ACTION");
        // We have a deterministic evaluation order, so the order of the actions
        // in the vector is guaranteed. The test rule generates the action by
        // iterating over the "srcs" field, so we get the actions in the order
        // of that field, not in alphabetical order.
        CHECK(result->Actions()[0]->ToJson()["input"]["in"]["data"]["path"] ==
              "simple_targets/foo.txt");
        CHECK(result->Actions()[1]->ToJson()["input"]["in"]["data"]["path"] ==
              "simple_targets/bar.txt");
    }
}

TEST_CASE("configuration deduplication", "[target_map]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());

    auto repo_config = SetupConfig();
    auto directory_entries =
        BuildMaps::Base::CreateDirectoryEntriesMap(&repo_config);
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries,
                                                         &repo_config);
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(&repo_config, 0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(&repo_config, 0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(&repo_config, 0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map,
                                                         &repo_config);
    auto rule_map =
        BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map, &repo_config);
    BuildMaps::Target::ResultTargetMap result_map{0};
    Statistics stats{};
    Progress exports_progress{};

    auto serve_config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(serve_config);

    Auth auth{};
    ApiBundle const apis{&storage_config.Get(),
                         &storage,
                         /*repo_config=*/nullptr,
                         &auth,
                         RemoteExecutionConfig::RemoteAddress()};
    auto serve = ServeApi::Create(*serve_config, &storage, &apis);
    AnalyseContext ctx{.repo_config = &repo_config,
                       .storage = &storage,
                       .statistics = &stats,
                       .progress = &exports_progress,
                       .serve = serve ? &*serve : nullptr};

    auto absent_target_variables_map =
        BuildMaps::Target::CreateAbsentTargetVariablesMap(&ctx, 0);

    auto absent_target_map = BuildMaps::Target::CreateAbsentTargetMap(
        &ctx, &result_map, &absent_target_variables_map, 0);

    auto target_map = BuildMaps::Target::CreateTargetMap(&ctx,
                                                         &source,
                                                         &targets_file_map,
                                                         &rule_map,
                                                         &directory_entries,
                                                         &absent_target_map,
                                                         &result_map);

    std::vector<AnalysedTargetPtr> result;
    bool error{false};
    std::string error_msg = "NONE";
    auto config = Configuration{Expression::FromJson(
        R"({"foo" : "bar", "irrelevant": "ignore me"})"_json)};
    auto alternative_config = Configuration{Expression::FromJson(
        R"({"foo" : "bar", "irrelevant": "other value"})"_json)};
    auto different_config =
        Configuration{Expression::FromJson(R"({"foo" : "baz"})"_json)};

    auto indirect_target = BuildMaps::Base::EntityName{
        "", "config_targets", "indirect dependency"};
    {
        TaskSystem ts;
        target_map.ConsumeAfterKeysReady(
            &ts,
            {BuildMaps::Target::ConfiguredTarget{.target = indirect_target,
                                                 .config = config},
             BuildMaps::Target::ConfiguredTarget{.target = indirect_target,
                                                 .config = alternative_config},
             BuildMaps::Target::ConfiguredTarget{.target = indirect_target,
                                                 .config = different_config}},
            [&result](auto values) {
                std::transform(values.begin(),
                               values.end(),
                               std::back_inserter(result),
                               [](auto* target) { return *target; });
            },
            [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                error = true;
                error_msg = msg;
            });
    }
    CHECK(not error);
    CHECK(error_msg == "NONE");
    CHECK(result[0]->Artifacts() == result[1]->Artifacts());
    CHECK(result[0]->Artifacts() != result[2]->Artifacts());
    Progress progress{};
    auto analysis_result = result_map.ToResult(&stats, &progress);
    CHECK(analysis_result.actions.size() == 2);
}

TEST_CASE("generator functions in string arguments", "[target_map]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());

    auto repo_config = SetupConfig();
    auto directory_entries =
        BuildMaps::Base::CreateDirectoryEntriesMap(&repo_config);
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries,
                                                         &repo_config);
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(&repo_config, 0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(&repo_config, 0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(&repo_config, 0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map,
                                                         &repo_config);
    auto rule_map =
        BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map, &repo_config);
    BuildMaps::Target::ResultTargetMap result_map{0};
    Statistics stats{};
    Progress exports_progress{};

    auto serve_config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(serve_config);

    Auth auth{};
    ApiBundle const apis{&storage_config.Get(),
                         &storage,
                         /*repo_config=*/nullptr,
                         &auth,
                         RemoteExecutionConfig::RemoteAddress()};
    auto serve = ServeApi::Create(*serve_config, &storage, &apis);
    AnalyseContext ctx{.repo_config = &repo_config,
                       .storage = &storage,
                       .statistics = &stats,
                       .progress = &exports_progress,
                       .serve = serve ? &*serve : nullptr};

    auto absent_target_variables_map =
        BuildMaps::Target::CreateAbsentTargetVariablesMap(&ctx, 0);

    auto absent_target_map = BuildMaps::Target::CreateAbsentTargetMap(
        &ctx, &result_map, &absent_target_variables_map, 0);

    auto target_map = BuildMaps::Target::CreateTargetMap(&ctx,
                                                         &source,
                                                         &targets_file_map,
                                                         &rule_map,
                                                         &directory_entries,
                                                         &absent_target_map,
                                                         &result_map);

    AnalysedTargetPtr result;
    bool error{false};
    std::string error_msg;
    auto empty_config = Configuration{Expression::FromJson(R"({})"_json)};

    SECTION("outs") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "artifact names"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["index.txt"]["type"] == "KNOWN");
        CHECK(result->Blobs()[0] == "bar.txt;baz.txt;foo.txt;link");
    }

    SECTION("runfies") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "runfile names"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["index.txt"]["type"] == "KNOWN");
        CHECK(result->Blobs()[0] == "bar.txt;baz.txt;foo.txt;link");
    }
}

TEST_CASE("built-in rules", "[target_map]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());

    auto repo_config = SetupConfig();
    auto directory_entries =
        BuildMaps::Base::CreateDirectoryEntriesMap(&repo_config);
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries,
                                                         &repo_config);
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(&repo_config, 0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(&repo_config, 0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(&repo_config, 0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map,
                                                         &repo_config);
    auto rule_map =
        BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map, &repo_config);
    BuildMaps::Target::ResultTargetMap result_map{0};
    Statistics stats{};
    Progress exports_progress{};

    auto serve_config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(serve_config);

    Auth auth{};
    ApiBundle const apis{&storage_config.Get(),
                         &storage,
                         /*repo_config=*/nullptr,
                         &auth,
                         RemoteExecutionConfig::RemoteAddress()};
    auto serve = ServeApi::Create(*serve_config, &storage, &apis);
    AnalyseContext ctx{.repo_config = &repo_config,
                       .storage = &storage,
                       .statistics = &stats,
                       .progress = &exports_progress,
                       .serve = serve ? &*serve : nullptr};

    auto absent_target_variables_map =
        BuildMaps::Target::CreateAbsentTargetVariablesMap(&ctx, 0);

    auto absent_target_map = BuildMaps::Target::CreateAbsentTargetMap(
        &ctx, &result_map, &absent_target_variables_map, 0);

    auto target_map = BuildMaps::Target::CreateTargetMap(&ctx,
                                                         &source,
                                                         &targets_file_map,
                                                         &rule_map,
                                                         &directory_entries,
                                                         &absent_target_map,
                                                         &result_map);
    AnalysedTargetPtr result;
    bool error{false};
    std::string error_msg;
    auto empty_config = Configuration{Expression::FromJson(R"({})"_json)};

    SECTION("generic") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "use generic"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->Map().size() == 1);
        CHECK(result->Artifacts()->ToJson()["out"]["type"] == "ACTION");
        CHECK(result->Artifacts()->ToJson()["out"]["data"]["path"] == "out");
    }

    SECTION("install") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "install"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts() == result->RunFiles());
        auto stage = result->Artifacts()->ToJson();
        CHECK(stage["foo.txt"]["type"] == "LOCAL");
        CHECK(stage["foo.txt"]["data"]["path"] == "simple_targets/foo.txt");
        CHECK(stage["bar.txt"]["type"] == "LOCAL");
        CHECK(stage["bar.txt"]["data"]["path"] == "simple_targets/bar.txt");
        CHECK(stage["bar.txt"]["type"] == "LOCAL");
        CHECK(stage["link"]["data"]["path"] == "simple_targets/link");
        CHECK(stage["combined.txt"]["type"] == "ACTION");
        CHECK(stage["combined.txt"]["data"]["path"] == "out");
        CHECK(stage["link_gen"]["type"] == "ACTION");
        CHECK(stage["link_gen"]["data"]["path"] == "sym");
        CHECK(stage["subdir/restaged.txt"]["type"] == "LOCAL");
        CHECK(stage["subdir/restaged.txt"]["data"]["path"] ==
              "simple_targets/bar.txt");
        CHECK(stage["mix/in/this/subdir/foo.txt"]["data"]["path"] ==
              "simple_targets/foo.txt");
        CHECK(stage["mix/in/this/subdir/bar.txt"]["data"]["path"] ==
              "simple_targets/bar.txt");
        CHECK(stage["mix/in/this/subdir/baz.txt"]["data"]["path"] ==
              "simple_targets/baz.txt");
        CHECK(stage["mix/in/this/subdir/link"]["data"]["path"] ==
              "simple_targets/link");
        CHECK(stage["mix/in/this/subdir/index.txt"]["type"] == "KNOWN");
    }

    SECTION("file_gen") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "generate file"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["generated.txt"]["type"] ==
              "KNOWN");
        CHECK(result->Blobs().size() == 1);
        CHECK(result->Blobs()[0] == "Hello World!");
    }

    SECTION("symlink") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "simple_targets", "generate symlink"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["generated_link"]["type"] ==
              "KNOWN");
        CHECK(result->Blobs().size() == 1);
        CHECK(result->Blobs()[0] == "dummy_link_target");
    }

    SECTION("configure") {
        auto target =
            BuildMaps::Base::EntityName{"", "config_targets", "bar in foo"};
        auto baz_config =
            empty_config.Update("bar", ExpressionPtr{std::string{"baz"}});
        error = false;
        error_msg = "NONE";
        AnalysedTargetPtr bar_result{};
        AnalysedTargetPtr baz_result{};
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{.target = target,
                                                     .config = empty_config},
                 BuildMaps::Target::ConfiguredTarget{.target = target,
                                                     .config = baz_config}},
                [&bar_result, &baz_result](auto values) {
                    bar_result = *values[0];
                    baz_result = *values[1];
                },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(bar_result->Artifacts()->ToJson()["foo.txt."]["type"] == "KNOWN");
        CHECK(bar_result->Artifacts()->ToJson()["foo.txt."]["data"]["id"] ==
              HashFunction::ComputeBlobHash("bar").HexString());
        CHECK(baz_result->Artifacts()->ToJson()["foo.txt."]["type"] == "KNOWN");
        CHECK(baz_result->Artifacts()->ToJson()["foo.txt."]["data"]["id"] ==
              HashFunction::ComputeBlobHash("baz").HexString());
    }
}

TEST_CASE("target reference", "[target_map]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());

    auto repo_config = SetupConfig();
    auto directory_entries =
        BuildMaps::Base::CreateDirectoryEntriesMap(&repo_config);
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries,
                                                         &repo_config);
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(&repo_config, 0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(&repo_config, 0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(&repo_config, 0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map,
                                                         &repo_config);
    auto rule_map =
        BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map, &repo_config);
    BuildMaps::Target::ResultTargetMap result_map{0};
    Statistics stats{};
    Progress exports_progress{};

    auto serve_config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(serve_config);

    Auth auth{};
    ApiBundle const apis{&storage_config.Get(),
                         &storage,
                         /*repo_config=*/nullptr,
                         &auth,
                         RemoteExecutionConfig::RemoteAddress()};
    auto serve = ServeApi::Create(*serve_config, &storage, &apis);
    AnalyseContext ctx{.repo_config = &repo_config,
                       .storage = &storage,
                       .statistics = &stats,
                       .progress = &exports_progress,
                       .serve = serve ? &*serve : nullptr};

    auto absent_target_variables_map =
        BuildMaps::Target::CreateAbsentTargetVariablesMap(&ctx, 0);

    auto absent_target_map = BuildMaps::Target::CreateAbsentTargetMap(
        &ctx, &result_map, &absent_target_variables_map, 0);

    auto target_map = BuildMaps::Target::CreateTargetMap(&ctx,
                                                         &source,
                                                         &targets_file_map,
                                                         &rule_map,
                                                         &directory_entries,
                                                         &absent_target_map,
                                                         &result_map);

    AnalysedTargetPtr result;
    bool error{false};
    std::string error_msg;
    auto empty_config = Configuration{Expression::FromJson(R"({})"_json)};

    SECTION("file vs target") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "file_reference", "hello.txt"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["hello.txt"]["type"] == "ACTION");
        CHECK(result->Artifacts()->ToJson()["hello.txt"]["data"]["path"] ==
              "hello.txt");

        CHECK(result->Actions().size() == 1);
        CHECK(result->Actions()[0]
                  ->ToJson()["input"]["raw_data/hello.txt"]["type"] == "LOCAL");
        CHECK(result->Actions()[0]
                  ->ToJson()["input"]["raw_data/hello.txt"]["data"]["path"] ==
              "file_reference/hello.txt");
    }

    SECTION("symlink vs target") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "symlink_reference", "link"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["link"]["type"] == "ACTION");
        CHECK(result->Artifacts()->ToJson()["link"]["data"]["path"] == "link");

        CHECK(result->Actions().size() == 1);
        CHECK(
            result->Actions()[0]->ToJson()["input"]["raw_data/link"]["type"] ==
            "LOCAL");
        CHECK(result->Actions()[0]
                  ->ToJson()["input"]["raw_data/link"]["data"]["path"] ==
              "symlink_reference/link");
    }

    SECTION("relative address") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{"", "x/x/x", "addressing"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["absolute"]["data"]["path"] ==
              "x/x/foo");
        CHECK(result->Artifacts()->ToJson()["relative"]["data"]["path"] ==
              "x/x/x/x/x/foo");
        CHECK(result->Artifacts()->ToJson()["upwards"]["data"]["path"] ==
              "x/foo");
    }
}

TEST_CASE("trees", "[target_map]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());

    auto repo_config = SetupConfig();
    auto directory_entries =
        BuildMaps::Base::CreateDirectoryEntriesMap(&repo_config);
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries,
                                                         &repo_config);
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(&repo_config, 0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(&repo_config, 0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(&repo_config, 0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map,
                                                         &repo_config);
    auto rule_map =
        BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map, &repo_config);
    BuildMaps::Target::ResultTargetMap result_map{0};
    Statistics stats{};
    Progress exports_progress{};

    auto serve_config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(serve_config);

    Auth auth{};
    ApiBundle const apis{&storage_config.Get(),
                         &storage,
                         /*repo_config=*/nullptr,
                         &auth,
                         RemoteExecutionConfig::RemoteAddress()};
    auto serve = ServeApi::Create(*serve_config, &storage, &apis);
    AnalyseContext ctx{.repo_config = &repo_config,
                       .storage = &storage,
                       .statistics = &stats,
                       .progress = &exports_progress,
                       .serve = serve ? &*serve : nullptr};

    auto absent_target_variables_map =
        BuildMaps::Target::CreateAbsentTargetVariablesMap(&ctx, 0);

    auto absent_target_map = BuildMaps::Target::CreateAbsentTargetMap(
        &ctx, &result_map, &absent_target_variables_map, 0);

    auto target_map = BuildMaps::Target::CreateTargetMap(&ctx,
                                                         &source,
                                                         &targets_file_map,
                                                         &rule_map,
                                                         &directory_entries,
                                                         &absent_target_map,
                                                         &result_map);

    AnalysedTargetPtr result;
    bool error{false};
    std::string error_msg;
    auto empty_config = Configuration{Expression::FromJson(R"({})"_json)};

    SECTION("no conflict") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{"", "tree", "no conflict"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Actions().size() == 1);
        CHECK(result->Actions()[0]->ToJson()["input"]["tree"]["type"] ==
              "TREE");
        CHECK(result->Actions()[0]->ToJson()["input"]["foo.txt"]["type"] ==
              "LOCAL");
        CHECK(result->Actions()[0]->ToJson()["input"]["foo.txt"]["data"]["pat"
                                                                         "h"] ==
              "tree/foo.txt");
        CHECK(result->Trees().size() == 1);
        CHECK(result->Trees()[0]->ToJson()["foo.txt"]["type"] == "LOCAL");
        CHECK(result->Trees()[0]->ToJson()["bar.txt"]["type"] == "LOCAL");
        CHECK(result->Trees()[0]->ToJson()["baz.txt"]["type"] == "LOCAL");
    }

    SECTION("stage into tree") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "tree", "range conflict"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
    }
}

TEST_CASE("RESULT error reporting", "[target_map]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());

    auto repo_config = SetupConfig();
    auto directory_entries =
        BuildMaps::Base::CreateDirectoryEntriesMap(&repo_config);
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries,
                                                         &repo_config);
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(&repo_config, 0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(&repo_config, 0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(&repo_config, 0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map,
                                                         &repo_config);
    auto rule_map =
        BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map, &repo_config);
    BuildMaps::Target::ResultTargetMap result_map{0};
    Statistics stats{};
    Progress exports_progress{};

    auto serve_config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(serve_config);

    Auth auth{};
    ApiBundle const apis{&storage_config.Get(),
                         &storage,
                         /*repo_config=*/nullptr,
                         &auth,
                         RemoteExecutionConfig::RemoteAddress()};
    auto serve = ServeApi::Create(*serve_config, &storage, &apis);
    AnalyseContext ctx{.repo_config = &repo_config,
                       .storage = &storage,
                       .statistics = &stats,
                       .progress = &exports_progress,
                       .serve = serve ? &*serve : nullptr};

    auto absent_target_variables_map =
        BuildMaps::Target::CreateAbsentTargetVariablesMap(&ctx, 0);

    auto absent_target_map = BuildMaps::Target::CreateAbsentTargetMap(
        &ctx, &result_map, &absent_target_variables_map, 0);

    auto target_map = BuildMaps::Target::CreateTargetMap(&ctx,
                                                         &source,
                                                         &targets_file_map,
                                                         &rule_map,
                                                         &directory_entries,
                                                         &absent_target_map,
                                                         &result_map);

    AnalysedTargetPtr result;
    bool error{false};
    std::string error_msg;
    auto empty_config = Configuration{Expression::FromJson(R"({})"_json)};

    SECTION("artifacts") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{"", "result", "artifacts"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
        CHECK(error_msg.find("artifacts-not-a-map") != std::string::npos);
    }

    SECTION("artifacts entry") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "result", "artifacts entry"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
        CHECK(error_msg.find("bad-artifact-entry") != std::string::npos);
        CHECK(error_msg.find("bad-artifact-path") != std::string::npos);
    }

    SECTION("runfiles") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{"", "result", "runfiles"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
        CHECK(error_msg.find("runfiles-not-a-map") != std::string::npos);
    }

    SECTION("runfiles entry") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "result", "runfiles entry"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
        CHECK(error_msg.find("bad-runfiles-entry") != std::string::npos);
        CHECK(error_msg.find("bad-runfiles-path") != std::string::npos);
    }

    SECTION("provides") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{"", "result", "provides"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
        CHECK(error_msg.find("provides-not-a-map") != std::string::npos);
    }
}

TEST_CASE("wrong arguments", "[target_map]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());

    auto repo_config = SetupConfig();
    auto directory_entries =
        BuildMaps::Base::CreateDirectoryEntriesMap(&repo_config);
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries,
                                                         &repo_config);
    auto targets_file_map =
        BuildMaps::Base::CreateTargetsFileMap(&repo_config, 0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(&repo_config, 0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(&repo_config, 0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map,
                                                         &repo_config);
    auto rule_map =
        BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map, &repo_config);
    BuildMaps::Target::ResultTargetMap result_map{0};
    Statistics stats{};
    Progress exports_progress{};

    auto serve_config = TestServeConfig::ReadFromEnvironment();
    REQUIRE(serve_config);

    Auth auth{};
    ApiBundle const apis{&storage_config.Get(),
                         &storage,
                         /*repo_config=*/nullptr,
                         &auth,
                         RemoteExecutionConfig::RemoteAddress()};
    auto serve = ServeApi::Create(*serve_config, &storage, &apis);
    AnalyseContext ctx{.repo_config = &repo_config,
                       .storage = &storage,
                       .statistics = &stats,
                       .progress = &exports_progress,
                       .serve = serve ? &*serve : nullptr};

    auto absent_target_variables_map =
        BuildMaps::Target::CreateAbsentTargetVariablesMap(&ctx, 0);

    auto absent_target_map = BuildMaps::Target::CreateAbsentTargetMap(
        &ctx, &result_map, &absent_target_variables_map, 0);

    auto target_map = BuildMaps::Target::CreateTargetMap(&ctx,
                                                         &source,
                                                         &targets_file_map,
                                                         &rule_map,
                                                         &directory_entries,
                                                         &absent_target_map,
                                                         &result_map);

    AnalysedTargetPtr result;
    bool error{false};
    std::string error_msg;
    auto empty_config = Configuration{Expression::FromJson(R"({})"_json)};

    SECTION("string field") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "bad_targets", "string field"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
        CHECK(error_msg.find("PlAiN sTrInG") != std::string::npos);
    }

    SECTION("string field 2") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "bad_targets", "string field 2"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
        CHECK(error_msg.find("4711") != std::string::npos);
    }

    SECTION("config field") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            "", "bad_targets", "config field"},
                    .config = empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(error);
        CHECK(error_msg != "NONE");
        CHECK(error_msg.find("FooKey") != std::string::npos);
        CHECK(error_msg.find("BarValue") != std::string::npos);
    }
}
