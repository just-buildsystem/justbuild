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
#include <functional>
#include <memory>
#include <string>
#include <utility>  // std::move

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/expression_map.hpp"
#include "src/buildtool/build_engine/base_maps/json_file_map.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "test/buildtool/build_engine/base_maps/test_repo.hpp"

namespace {

using namespace BuildMaps::Base;  // NOLINT

auto SetupConfig(bool use_git) -> RepositoryConfig {
    auto root = FileRoot{kBasePath / "data_rule"};
    if (use_git) {
        auto repo_path = CreateTestRepo();
        REQUIRE(repo_path);
        auto git_root = FileRoot::FromGit(*repo_path, kRuleTreeId);
        REQUIRE(git_root);
        root = std::move(*git_root);
    }
    RepositoryConfig repo_config{};
    repo_config.SetInfo("", RepositoryConfig::RepositoryInfo{root});
    return repo_config;
}

auto ReadUserRule(EntityName const& id,
                  UserRuleMap::Consumer value_checker,
                  bool use_git = false) -> bool {
    auto repo_config = SetupConfig(use_git);
    auto expr_file_map = CreateExpressionFileMap(&repo_config, 0);
    auto expr_func_map = CreateExpressionMap(&expr_file_map, &repo_config);
    auto rule_file_map = CreateRuleFileMap(&repo_config, 0);
    auto user_rule_map =
        CreateRuleMap(&rule_file_map, &expr_func_map, &repo_config);

    bool success{true};
    {
        TaskSystem ts;
        user_rule_map.ConsumeAfterKeysReady(
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

TEST_CASE("Test empty rule", "[expression_map]") {
    auto name = EntityName{"", ".", "test_empty_rule"};
    auto consumer = [](auto values) { REQUIRE(values[0]); };

    SECTION("via file") {
        CHECK(ReadUserRule(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK(ReadUserRule(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Test rule fields", "[rule_map]") {
    auto name = EntityName{"", ".", "test_rule_fields"};
    auto consumer = [](auto values) {
        REQUIRE(*values[0]);
        REQUIRE_FALSE((*values[0])->StringFields().empty());
        REQUIRE_FALSE((*values[0])->TargetFields().empty());
        REQUIRE_FALSE((*values[0])->ConfigFields().empty());
        CHECK((*values[0])->StringFields().at(0) == "foo");
        CHECK((*values[0])->TargetFields().at(0) == "bar");
        CHECK((*values[0])->ConfigFields().at(0) == "baz");
    };

    SECTION("via file") {
        CHECK(ReadUserRule(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK(ReadUserRule(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Test config_transitions target", "[rule_map]") {
    auto consumer = [](auto values) { REQUIRE(*values[0]); };

    SECTION("via field") {
        auto name =
            EntityName{"", ".", "test_config_transitions_target_via_field"};

        SECTION("via file") {
            CHECK(ReadUserRule(name, consumer, /*use_git=*/false));
        }

        SECTION("via git tree") {
            CHECK(ReadUserRule(name, consumer, /*use_git=*/true));
        }
    }
    SECTION("via implicit") {
        auto name =
            EntityName{"", ".", "test_config_transitions_target_via_implicit"};

        SECTION("via file") {
            CHECK(ReadUserRule(name, consumer, /*use_git=*/false));
        }

        SECTION("via git tree") {
            CHECK(ReadUserRule(name, consumer, /*use_git=*/true));
        }
    }
}

TEST_CASE("Test config_transitions canonicalness", "[rule_map]") {
    auto name = EntityName{"", ".", "test_config_transitions_canonicalness"};
    auto consumer = [](auto values) {
        REQUIRE(*values[0]);
        auto const& transitions = (*values[0])->ConfigTransitions();
        REQUIRE(transitions.size() == 4);
        REQUIRE(transitions.at("foo"));
        REQUIRE(transitions.at("bar"));
        REQUIRE(transitions.at("baz"));
        REQUIRE(transitions.at("qux"));
        auto foo = transitions.at("foo")->Evaluate({}, {});
        auto bar = transitions.at("bar")->Evaluate({}, {});
        auto baz = transitions.at("baz")->Evaluate({}, {});
        auto qux = transitions.at("qux")->Evaluate({}, {});
        CHECK(foo == Expression::FromJson(R"([{}])"_json));
        CHECK(bar == Expression::FromJson(R"([{"exists": true}])"_json));
        CHECK(baz == Expression::FromJson(R"([{}])"_json));
        CHECK(qux == Expression::FromJson(R"([{"defined": true}])"_json));
    };

    SECTION("via file") {
        CHECK(ReadUserRule(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK(ReadUserRule(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Test call of imported expression", "[rule_map]") {
    auto name = EntityName{"", ".", "test_call_import"};
    auto consumer = [](auto values) {
        REQUIRE(*values[0]);
        auto expr = (*values[0])->Expression();

        REQUIRE(expr);
        auto result = expr->Evaluate(
            Configuration{Expression::FromJson(R"({"FOO": "bar"})"_json)}, {});

        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result["type"] == Expression{std::string{"RESULT"}});
        CHECK(result["artifacts"] ==
              Expression::FromJson(R"({"foo": "bar"})"_json));
    };

    SECTION("via file") {
        CHECK(ReadUserRule(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK(ReadUserRule(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Fail due to unknown ID", "[rule_map]") {
    auto name = EntityName{"", ".", "does_not_exist"};
    auto consumer = [](auto /*values*/) {
        CHECK(false);  // should never be called
    };

    SECTION("via file") {
        CHECK_FALSE(ReadUserRule(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK_FALSE(ReadUserRule(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Fail due to conflicting keyword names", "[rule_map]") {
    SECTION("string_fields") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_string_kw_conflict"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }
    SECTION("target_fields") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_target_kw_conflict"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }
    SECTION("config_fields") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_config_kw_conflict"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }
    SECTION("implicit_fields") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_implicit_kw_conflict"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }
}

TEST_CASE("Fail due to conflicting field names", "[rule_map]") {
    SECTION("string <-> target") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_string_target_conflict"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }
    SECTION("target <-> config") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_target_config_conflict"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }
    SECTION("config <-> implicit") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_config_implicit_conflict"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }
}

TEST_CASE("Fail due to unknown config_transitions target", "[rule_map]") {
    CHECK_FALSE(
        ReadUserRule({"", ".", "test_unknown_config_transitions_target"},
                     [](auto /*values*/) {
                         CHECK(false);  // should never be called
                     }));
}

TEST_CASE("missing config_vars", "[rule_map]") {
    CHECK(ReadUserRule({"", ".", "test_missing_config_vars"}, [](auto values) {
        REQUIRE(*values[0]);
        auto expr = (*values[0])->Expression();

        REQUIRE(expr);
        auto result = expr->Evaluate(
            Configuration{Expression::FromJson(R"({"FOO": "bar"})"_json)}, {});

        CHECK(result["artifacts"]["foo"] ==
              Expression::FromJson(R"(null)"_json));
    }));
}

TEST_CASE("Fail due to missing imports", "[rule_map]") {
    CHECK(ReadUserRule({"", ".", "test_missing_imports"}, [](auto values) {
        REQUIRE(*values[0]);
        auto expr = (*values[0])->Expression();

        REQUIRE(expr);
        auto result = expr->Evaluate(
            Configuration{Expression::FromJson(R"({"FOO": "bar"})"_json)}, {});

        CHECK_FALSE(result);
    }));
}

TEST_CASE("Malformed rule description", "[rule_map]") {
    SECTION("Malformed rule") {
        CHECK_FALSE(
            ReadUserRule({"", ".", "test_malformed_rule"}, [](auto /*values*/) {
                CHECK(false);  // should never be called
            }));
    }

    SECTION("Malformed rule expression") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_rule_expression"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }

    SECTION("Malformed target_fields") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_target_fields"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }

    SECTION("Malformed string_fields") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_string_fields"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }

    SECTION("Malformed config_fields") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_config_fields"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }

    SECTION("Malformed implicit") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_implicit"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }

    SECTION("Malformed implicit entry") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_implicit_entry"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }

    SECTION("Malformed implicit entity name") {
        CHECK_FALSE(
            ReadUserRule({"", ".", "test_malformed_implicit_entity_name"},
                         [](auto /*values*/) {
                             CHECK(false);  // should never be called
                         }));
        CHECK_FALSE(
            ReadUserRule({"", ".", "test_malformed_implicit_entity_name_2"},
                         [](auto /*values*/) {
                             CHECK(false);  // should never be called
                         }));
    }

    SECTION("Malformed config_vars") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_config_vars"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }

    SECTION("Malformed config_transitions") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_config_transitions"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }

    SECTION("Malformed imports") {
        CHECK_FALSE(ReadUserRule({"", ".", "test_malformed_imports"},
                                 [](auto /*values*/) {
                                     CHECK(false);  // should never be called
                                 }));
    }
}
