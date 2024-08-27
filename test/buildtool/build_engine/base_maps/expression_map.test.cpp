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

#include "src/buildtool/build_engine/base_maps/expression_map.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>  // std::move

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/json_file_map.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "test/buildtool/build_engine/base_maps/test_repo.hpp"

namespace {

using namespace BuildMaps::Base;  // NOLINT

auto SetupConfig(bool use_git) -> RepositoryConfig {
    auto root = FileRoot{kBasePath / "data_expr"};
    if (use_git) {
        auto repo_path = CreateTestRepo();
        REQUIRE(repo_path);
        auto git_root = FileRoot::FromGit(*repo_path, kExprTreeId);
        REQUIRE(git_root);
        root = std::move(*git_root);
    }
    RepositoryConfig repo_config{};
    repo_config.SetInfo("", RepositoryConfig::RepositoryInfo{root});
    return repo_config;
}

auto ReadExpressionFunction(EntityName const& id,
                            ExpressionFunctionMap::Consumer value_checker,
                            bool use_git = false) -> bool {
    auto repo_config = SetupConfig(use_git);
    auto expr_file_map = CreateExpressionFileMap(&repo_config, 0);
    auto expr_func_map = CreateExpressionMap(&expr_file_map, &repo_config);

    bool success{true};
    {
        TaskSystem ts;
        expr_func_map.ConsumeAfterKeysReady(
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

TEST_CASE("Simple expression object literal", "[expression_map]") {
    auto name = EntityName{"", ".", "test_expression_literal"};
    auto consumer = [](auto values) {
        REQUIRE(*values[0]);
        auto expr = (*values[0])->Evaluate({}, {});

        REQUIRE(expr);
        REQUIRE(expr->IsString());
        CHECK(expr == Expression::FromJson(R"("foo")"_json));
    };

    SECTION("via file") {
        CHECK(ReadExpressionFunction(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK(ReadExpressionFunction(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Simple read of variable", "[expression_map]") {
    auto name = EntityName{"", ".", "test_read_vars"};
    auto consumer = [](auto values) {
        REQUIRE(*values[0]);
        auto expr = (*values[0])
                        ->Evaluate(Configuration{Expression::FromJson(
                                       R"({"FOO": "bar"})"_json)},
                                   {});

        REQUIRE(expr);
        REQUIRE(expr->IsString());
        CHECK(expr == Expression{std::string{"bar"}});
    };

    SECTION("via file") {
        CHECK(ReadExpressionFunction(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK(ReadExpressionFunction(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Simple call of imported expression", "[expression_map]") {
    auto name = EntityName{"", ".", "test_call_import"};
    auto consumer = [](auto values) {
        REQUIRE(*values[0]);
        auto expr = (*values[0])
                        ->Evaluate(Configuration{Expression::FromJson(
                                       R"({"FOO": "bar"})"_json)},
                                   {});

        REQUIRE(expr);
        REQUIRE(expr->IsString());
        CHECK(expr == Expression{std::string{"bar"}});
    };

    SECTION("via file") {
        CHECK(ReadExpressionFunction(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK(ReadExpressionFunction(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Overwrite import in nested expression", "[expression_map]") {
    auto name = EntityName{"", ".", "test_overwrite_import"};
    auto consumer = [](auto values) {
        REQUIRE(*values[0]);
        auto expr = (*values[0])
                        ->Evaluate(Configuration{Expression::FromJson(
                                       R"({"FOO": "bar"})"_json)},
                                   {});

        REQUIRE(expr);
        REQUIRE(expr->IsString());
        CHECK(expr == Expression{std::string{"bar"}});
    };

    SECTION("via file") {
        CHECK(ReadExpressionFunction(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK(ReadExpressionFunction(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Fail due to unkown ID", "[expression_map]") {
    auto name = EntityName{"", ".", "does_not_exist"};
    auto consumer = [](auto /*values*/) {
        CHECK(false);  // should never be called
    };

    SECTION("via file") {
        CHECK_FALSE(ReadExpressionFunction(name, consumer, /*use_git=*/false));
    }

    SECTION("via git tree") {
        CHECK_FALSE(ReadExpressionFunction(name, consumer, /*use_git=*/true));
    }
}

TEST_CASE("Fail due to missing vars", "[expression_map]") {
    CHECK(
        ReadExpressionFunction({"", ".", "test_missing_vars"}, [](auto values) {
            REQUIRE(*values[0]);
            auto expr = (*values[0])
                            ->Evaluate(Configuration{Expression::FromJson(
                                           R"({"FOO": "bar"})"_json)},
                                       {});

            CHECK(expr == Expression::FromJson(R"(null)"_json));
        }));
}

TEST_CASE("Fail due to missing imports", "[expression_map]") {
    CHECK(ReadExpressionFunction(
        {"", ".", "test_missing_imports"}, [](auto values) {
            REQUIRE(*values[0]);
            auto expr = (*values[0])
                            ->Evaluate(Configuration{Expression::FromJson(
                                           R"({"FOO": "bar"})"_json)},
                                       {});

            CHECK_FALSE(expr);
        }));
}

TEST_CASE("Malformed function", "[expression_map]") {
    CHECK_FALSE(ReadExpressionFunction(
        {"", ".", "test_malformed_function"}, [](auto /*values*/) {
            CHECK(false);  // should never be called
        }));
}

TEST_CASE("Malformed expression", "[expression_map]") {
    CHECK_FALSE(ReadExpressionFunction(
        {"", ".", "test_malformed_expression"}, [](auto /*values*/) {
            CHECK(false);  // should never be called
        }));
}

TEST_CASE("Malformed vars", "[expression_map]") {
    CHECK_FALSE(ReadExpressionFunction(
        {"", ".", "test_malformed_vars"}, [](auto /*values*/) {
            CHECK(false);  // should never be called
        }));
}

TEST_CASE("Malformed imports", "[expression_map]") {
    CHECK_FALSE(ReadExpressionFunction(
        {"", ".", "test_malformed_imports"}, [](auto /*values*/) {
            CHECK(false);  // should never be called
        }));
}
