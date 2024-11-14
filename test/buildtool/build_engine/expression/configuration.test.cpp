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

#include "src/buildtool/build_engine/expression/configuration.hpp"

#include <string>
#include <utility>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"

TEST_CASE("Access", "[configuration]") {
    auto env =
        Configuration{Expression::FromJson(R"({"foo": 1, "bar": 2})"_json)};

    CHECK(env["foo"] == Expression::FromJson("1"_json));
    CHECK(env[Expression::FromJson(R"("bar")"_json)] ==
          Expression::FromJson("2"_json));

    CHECK(env["baz"] == Expression::FromJson(R"(null)"_json));
    CHECK(env[Expression::FromJson(R"("baz")"_json)] ==
          Expression::FromJson(R"(null)"_json));
}

TEST_CASE("Update", "[configuration]") {
    SECTION("Append") {
        auto env = Configuration{Expression::FromJson(R"({})"_json)};
        env = env.Update(Expression::FromJson(R"({"foo": 1})"_json));
        CHECK(env["foo"] == Expression::FromJson("1"_json));

        env = env.Update("bar", Expression::number_t{2});
        CHECK(env["bar"] == Expression::FromJson("2"_json));

        env = env.Update(Expression::map_t::underlying_map_t{
            {"baz", ExpressionPtr{Expression::number_t{3}}}});
        CHECK(env["baz"] == Expression::FromJson("3"_json));
    }

    SECTION("Overwrite") {
        auto env = Configuration{
            Expression::FromJson(R"({"foo": 1, "bar": 2, "baz" : 3})"_json)};
        CHECK(env["foo"] == Expression::FromJson("1"_json));
        CHECK(env["bar"] == Expression::FromJson("2"_json));
        CHECK(env["baz"] == Expression::FromJson("3"_json));

        env = env.Update(Expression::FromJson(R"({"foo": 10})"_json));
        CHECK(env["foo"] == Expression::FromJson("10"_json));
        CHECK(env["bar"] == Expression::FromJson("2"_json));
        CHECK(env["baz"] == Expression::FromJson("3"_json));

        env = env.Update("bar", Expression::number_t{20});  // NOLINT
        CHECK(env["foo"] == Expression::FromJson("10"_json));
        CHECK(env["bar"] == Expression::FromJson("20"_json));
        CHECK(env["baz"] == Expression::FromJson("3"_json));

        env = env.Update(Expression::map_t::underlying_map_t{
            {"baz", ExpressionPtr{Expression::number_t{30}}}});  // NOLINT
        CHECK(env["foo"] == Expression::FromJson("10"_json));
        CHECK(env["bar"] == Expression::FromJson("20"_json));
        CHECK(env["baz"] == Expression::FromJson("30"_json));
    }
}

TEST_CASE("Prune", "[configuration]") {
    auto env =
        Configuration{Expression::FromJson(R"({"foo": 1, "bar": 2})"_json)};
    CHECK(env["foo"] == Expression::FromJson("1"_json));
    CHECK(env["bar"] == Expression::FromJson("2"_json));

    SECTION("Via string list") {
        env = env.Prune(std::vector<std::string>{"foo", "bar", "baz"});
        CHECK(env["foo"] == Expression::FromJson("1"_json));
        CHECK(env["bar"] == Expression::FromJson("2"_json));

        env = env.Prune(std::vector<std::string>{"foo", "bar"});
        CHECK(env["foo"] == Expression::FromJson("1"_json));
        CHECK(env["bar"] == Expression::FromJson("2"_json));

        env = env.Prune(std::vector<std::string>{"foo"});
        CHECK(env["foo"] == Expression::FromJson("1"_json));
        CHECK(env["bar"] == Expression::FromJson(R"(null)"_json));

        env = env.Prune(std::vector<std::string>{});
        CHECK(env["foo"] == Expression::FromJson(R"(null)"_json));
        CHECK(env["bar"] == Expression::FromJson(R"(null)"_json));
    }

    SECTION("Via expression") {
        env = env.Prune(Expression::FromJson(R"(["foo", "bar", "baz"])"_json));
        CHECK(env["foo"] == Expression::FromJson("1"_json));
        CHECK(env["bar"] == Expression::FromJson("2"_json));

        env = env.Prune(Expression::FromJson(R"(["foo", "bar"])"_json));
        CHECK(env["foo"] == Expression::FromJson("1"_json));
        CHECK(env["bar"] == Expression::FromJson("2"_json));

        env = env.Prune(Expression::FromJson(R"(["foo"])"_json));
        CHECK(env["foo"] == Expression::FromJson("1"_json));
        CHECK(env["bar"] == Expression::FromJson(R"(null)"_json));

        env = env.Prune(Expression::FromJson(R"([])"_json));
        CHECK(env["foo"] == Expression::FromJson(R"(null)"_json));
        CHECK(env["bar"] == Expression::FromJson(R"(null)"_json));

        CHECK_THROWS_AS(env.Prune(Expression::FromJson(
                            R"(["not_all_string", false])"_json)),
                        Expression::ExpressionTypeError);

        CHECK_THROWS_AS(env.Prune(Expression::FromJson(R"("not_a_list")"_json)),
                        Expression::ExpressionTypeError);
    }
}
