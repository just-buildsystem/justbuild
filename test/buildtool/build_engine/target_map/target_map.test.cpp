#include "catch2/catch.hpp"
#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/expression_map.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/base_maps/source_map.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

namespace {

using none_t = Expression::none_t;

void SetupConfig() {
    auto info = RepositoryConfig::RepositoryInfo{
        FileRoot{"test/buildtool/build_engine/target_map/data_src"},
        FileRoot{"test/buildtool/build_engine/target_map/data_targets"},
        FileRoot{"test/buildtool/build_engine/target_map/data_rules"},
        FileRoot{"test/buildtool/build_engine/target_map/data_expr"}};
    RepositoryConfig::Instance().Reset();
    RepositoryConfig::Instance().SetInfo("", std::move(info));
}

}  // namespace

TEST_CASE("simple targets") {
    SetupConfig();
    auto directory_entries = BuildMaps::Base::CreateDirectoryEntriesMap();
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries);
    auto targets_file_map = BuildMaps::Base::CreateTargetsFileMap(0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map);
    auto rule_map = BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map);
    BuildMaps::Target::ResultTargetMap result_map{0};
    auto target_map = BuildMaps::Target::CreateTargetMap(
        &source, &targets_file_map, &rule_map, &directory_entries, &result_map);

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
                    BuildMaps::Base::EntityName{
                        "", "a/b/targets_here", "c/d/foo"},
                    empty_config}},
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

    SECTION("No targets file here") {
        {
            error_msg = "NONE";
            error = false;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    BuildMaps::Base::EntityName{
                        "", "a/b/targets_here/c", "d/foo"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "rule just provides"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "rule provides FOO"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "rule provides FOO"},
                    config}},
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "config transition for FOO"},
                    config}},
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "collect dep artifacts"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "stage blob"},
                    empty_config}},
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

    SECTION("Stage implicit target") {
        {
            error_msg = "NONE";
            error = false;
            result = nullptr;

            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "use implicit"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "actions"},
                    empty_config}},
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

TEST_CASE("configuration deduplication") {
    SetupConfig();
    auto directory_entries = BuildMaps::Base::CreateDirectoryEntriesMap();
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries);
    auto targets_file_map = BuildMaps::Base::CreateTargetsFileMap(0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map);
    auto rule_map = BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map);
    BuildMaps::Target::ResultTargetMap result_map{0};
    auto target_map = BuildMaps::Target::CreateTargetMap(
        &source, &targets_file_map, &rule_map, &directory_entries, &result_map);

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
            {BuildMaps::Target::ConfiguredTarget{indirect_target, config},
             BuildMaps::Target::ConfiguredTarget{indirect_target,
                                                 alternative_config},
             BuildMaps::Target::ConfiguredTarget{indirect_target,
                                                 different_config}},
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
    auto analysis_result = result_map.ToResult();
    CHECK(analysis_result.actions.size() == 2);
}

TEST_CASE("generator functions in string arguments") {
    SetupConfig();
    auto directory_entries = BuildMaps::Base::CreateDirectoryEntriesMap();
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries);
    auto targets_file_map = BuildMaps::Base::CreateTargetsFileMap(0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map);
    auto rule_map = BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map);
    BuildMaps::Target::ResultTargetMap result_map{0};
    auto target_map = BuildMaps::Target::CreateTargetMap(
        &source, &targets_file_map, &rule_map, &directory_entries, &result_map);

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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "artifact names"},
                    empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["index.txt"]["type"] == "KNOWN");
        CHECK(result->Blobs()[0] == "bar.txt;baz.txt;foo.txt");
    }

    SECTION("runfies") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "runfile names"},
                    empty_config}},
                [&result](auto values) { result = *values[0]; },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK(!error);
        CHECK(error_msg == "NONE");
        CHECK(result->Artifacts()->ToJson()["index.txt"]["type"] == "KNOWN");
        CHECK(result->Blobs()[0] == "bar.txt;baz.txt;foo.txt");
    }
}

TEST_CASE("built-in rules") {
    SetupConfig();
    auto directory_entries = BuildMaps::Base::CreateDirectoryEntriesMap();
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries);
    auto targets_file_map = BuildMaps::Base::CreateTargetsFileMap(0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map);
    auto rule_map = BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map);
    BuildMaps::Target::ResultTargetMap result_map{0};
    auto target_map = BuildMaps::Target::CreateTargetMap(
        &source, &targets_file_map, &rule_map, &directory_entries, &result_map);

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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "use generic"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "install"},
                    empty_config}},
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
        CHECK(stage["combined.txt"]["type"] == "ACTION");
        CHECK(stage["combined.txt"]["data"]["path"] == "out");
        CHECK(stage["subdir/restaged.txt"]["type"] == "LOCAL");
        CHECK(stage["subdir/restaged.txt"]["data"]["path"] ==
              "simple_targets/bar.txt");
        CHECK(stage["mix/in/this/subdir/foo.txt"]["data"]["path"] ==
              "simple_targets/foo.txt");
        CHECK(stage["mix/in/this/subdir/bar.txt"]["data"]["path"] ==
              "simple_targets/bar.txt");
        CHECK(stage["mix/in/this/subdir/baz.txt"]["data"]["path"] ==
              "simple_targets/baz.txt");
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
                    BuildMaps::Base::EntityName{
                        "", "simple_targets", "generate file"},
                    empty_config}},
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
}

TEST_CASE("target reference") {
    SetupConfig();
    auto directory_entries = BuildMaps::Base::CreateDirectoryEntriesMap();
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries);
    auto targets_file_map = BuildMaps::Base::CreateTargetsFileMap(0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map);
    auto rule_map = BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map);
    BuildMaps::Target::ResultTargetMap result_map{0};
    auto target_map = BuildMaps::Target::CreateTargetMap(
        &source, &targets_file_map, &rule_map, &directory_entries, &result_map);

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
                    BuildMaps::Base::EntityName{
                        "", "file_reference", "hello.txt"},
                    empty_config}},
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

    SECTION("relative address") {
        error = false;
        error_msg = "NONE";
        {
            TaskSystem ts;
            target_map.ConsumeAfterKeysReady(
                &ts,
                {BuildMaps::Target::ConfiguredTarget{
                    BuildMaps::Base::EntityName{"", "x/x/x", "addressing"},
                    empty_config}},
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

TEST_CASE("trees") {
    SetupConfig();
    auto directory_entries = BuildMaps::Base::CreateDirectoryEntriesMap();
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries);
    auto targets_file_map = BuildMaps::Base::CreateTargetsFileMap(0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map);
    auto rule_map = BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map);
    BuildMaps::Target::ResultTargetMap result_map{0};
    auto target_map = BuildMaps::Target::CreateTargetMap(
        &source, &targets_file_map, &rule_map, &directory_entries, &result_map);

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
                    BuildMaps::Base::EntityName{"", "tree", "no conflict"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{"", "tree", "range conflict"},
                    empty_config}},
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

TEST_CASE("RESULT error reporting") {
    SetupConfig();
    auto directory_entries = BuildMaps::Base::CreateDirectoryEntriesMap();
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries);
    auto targets_file_map = BuildMaps::Base::CreateTargetsFileMap(0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map);
    auto rule_map = BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map);
    BuildMaps::Target::ResultTargetMap result_map{0};
    auto target_map = BuildMaps::Target::CreateTargetMap(
        &source, &targets_file_map, &rule_map, &directory_entries, &result_map);

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
                    BuildMaps::Base::EntityName{"", "result", "artifacts"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "result", "artifacts entry"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{"", "result", "runfiles"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{"", "result", "runfiles entry"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{"", "result", "provides"},
                    empty_config}},
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

TEST_CASE("wrong arguments") {
    SetupConfig();
    auto directory_entries = BuildMaps::Base::CreateDirectoryEntriesMap();
    auto source = BuildMaps::Base::CreateSourceTargetMap(&directory_entries);
    auto targets_file_map = BuildMaps::Base::CreateTargetsFileMap(0);
    auto rule_file_map = BuildMaps::Base::CreateRuleFileMap(0);
    static auto expressions_file_map =
        BuildMaps::Base::CreateExpressionFileMap(0);
    auto expr_map = BuildMaps::Base::CreateExpressionMap(&expressions_file_map);
    auto rule_map = BuildMaps::Base::CreateRuleMap(&rule_file_map, &expr_map);
    BuildMaps::Target::ResultTargetMap result_map{0};
    auto target_map = BuildMaps::Target::CreateTargetMap(
        &source, &targets_file_map, &rule_map, &directory_entries, &result_map);

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
                    BuildMaps::Base::EntityName{
                        "", "bad_targets", "string field"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "bad_targets", "string field 2"},
                    empty_config}},
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
                    BuildMaps::Base::EntityName{
                        "", "bad_targets", "config field"},
                    empty_config}},
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
