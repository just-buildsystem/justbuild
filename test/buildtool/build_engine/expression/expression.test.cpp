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

#include "src/buildtool/build_engine/expression/expression.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_all.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "test/utils/container_matchers.hpp"

TEST_CASE("Expression access", "[expression]") {  // NOLINT
    using namespace std::string_literals;
    using path = std::filesystem::path;
    using none_t = Expression::none_t;
    using number_t = Expression::number_t;
    using artifact_t = Expression::artifact_t;
    using result_t = Expression::result_t;
    using list_t = Expression::list_t;
    using map_t = Expression::map_t;

    auto none = ExpressionPtr{};
    auto boolean = ExpressionPtr{true};
    auto number = ExpressionPtr{number_t{1}};
    auto string = ExpressionPtr{"2"s};
    auto artifact =
        ExpressionPtr{ArtifactDescription::CreateTree(path{"local_path"})};
    auto result = ExpressionPtr{result_t{boolean, number, string}};
    auto list = ExpressionPtr{list_t{number}};
    auto map = ExpressionPtr{map_t{{"3"s, number}}};

    SECTION("Type checks") {
        CHECK(none->IsNone());

        CHECK(boolean->IsBool());
        CHECK_FALSE(boolean->IsNone());

        CHECK(number->IsNumber());
        CHECK_FALSE(number->IsNone());

        CHECK(string->IsString());
        CHECK_FALSE(string->IsNone());

        CHECK(artifact->IsArtifact());
        CHECK_FALSE(artifact->IsNone());

        CHECK(result->IsResult());
        CHECK_FALSE(result->IsNone());

        CHECK(list->IsList());
        CHECK_FALSE(list->IsNone());

        CHECK(map->IsMap());
        CHECK_FALSE(map->IsNone());
    }

    SECTION("Throwing accessors") {
        CHECK(boolean->Bool() == true);
        CHECK_THROWS_AS(boolean->Number(), Expression::ExpressionTypeError);

        CHECK(number->Number() == number_t{1});
        CHECK_THROWS_AS(number->Bool(), Expression::ExpressionTypeError);

        CHECK(string->String() == "2"s);
        CHECK_THROWS_AS(string->Artifact(), Expression::ExpressionTypeError);

        CHECK(artifact->Artifact() ==
              ArtifactDescription::CreateTree(path{"local_path"}));
        CHECK_THROWS_AS(artifact->String(), Expression::ExpressionTypeError);

        CHECK(result->Result() == result_t{boolean, number, string});
        CHECK_THROWS_AS(result->String(), Expression::ExpressionTypeError);

        CHECK_THAT(list->List(),
                   Catch::Matchers::Equals<ExpressionPtr>({number}));
        CHECK_THROWS_AS(list->Map(), Expression::ExpressionTypeError);

        REQUIRE(map->Map().at("3"s) == number);
        CHECK_THROWS_AS(map->List(), Expression::ExpressionTypeError);
    }

    SECTION("Non-throwing accessors") {
        CHECK(none->Value<none_t>());

        CHECK(boolean->Value<bool>());
        CHECK_FALSE(boolean->Value<none_t>());

        CHECK(number->Value<number_t>());
        CHECK_FALSE(number->Value<none_t>());

        CHECK(string->Value<std::string>());
        CHECK_FALSE(string->Value<none_t>());

        CHECK(artifact->Value<artifact_t>());
        CHECK_FALSE(artifact->Value<none_t>());

        CHECK(result->Value<result_t>());
        CHECK_FALSE(result->Value<none_t>());

        CHECK(list->Value<list_t>());
        CHECK_FALSE(list->Value<none_t>());

        CHECK(map->Value<map_t>());
        CHECK_FALSE(map->Value<none_t>());
    }

    SECTION("Non-throwing comparison operator") {
        CHECK(none == none);
        CHECK(none == Expression{});
        CHECK(none == Expression::FromJson("null"_json));
        CHECK(none != Expression{false});
        CHECK(none != Expression{number_t{0}});
        CHECK(none != Expression{""s});
        CHECK(none != Expression{"0"s});
        CHECK(none != Expression{list_t{}});
        CHECK(none != Expression{map_t{}});

        CHECK(boolean == boolean);
        CHECK(boolean == true);
        CHECK(boolean == Expression{true});
        CHECK(boolean == Expression::FromJson("true"_json));
        CHECK(boolean != false);
        CHECK(boolean != Expression{false});
        CHECK(boolean != number_t{1});
        CHECK(boolean != number);
        CHECK(boolean != Expression::FromJson("false"_json));

        CHECK(number == number);
        CHECK(number == number_t{1});
        CHECK(number == Expression{number_t{1}});
        CHECK(number == Expression::FromJson("1"_json));
        CHECK(number != number_t{});
        CHECK(number != Expression{number_t{}});
        CHECK(number != true);
        CHECK(number != boolean);
        CHECK(number != Expression::FromJson("0"_json));

        CHECK(string == string);
        CHECK(string == "2"s);
        CHECK(string == Expression{"2"s});
        CHECK(string == Expression::FromJson(R"("2")"_json));
        CHECK(string != ""s);
        CHECK(string != Expression{""s});
        CHECK(string != ArtifactDescription::CreateTree(path{"local_path"}));
        CHECK(string != artifact);
        CHECK(string != Expression::FromJson(R"("")"_json));

        CHECK(artifact == artifact);
        CHECK(artifact == ArtifactDescription::CreateTree(path{"local_path"}));
        CHECK(artifact ==
              Expression{ArtifactDescription::CreateTree(path{"local_path"})});
        CHECK(artifact != ""s);
        CHECK(artifact != string);

        CHECK(result == result);
        CHECK(result == result_t{boolean, number, string});
        CHECK(result == Expression{result_t{boolean, number, string}});
        CHECK(result != ""s);
        CHECK(result != string);

        CHECK(list == list);
        CHECK(list == list_t{number});
        CHECK(list == Expression{list_t{number}});
        CHECK(list == Expression::FromJson("[1]"_json));
        CHECK(list != list_t{});
        CHECK(list != Expression{list_t{}});
        CHECK(list != map);
        CHECK(list != *map);
        CHECK(list != Expression::FromJson(R"({"1":1})"_json));

        CHECK(map == map);
        CHECK(map == map_t{{"3"s, number}});
        CHECK(map == Expression{map_t{{"3"s, number}}});
        CHECK(map == Expression::FromJson(R"({"3":1})"_json));
        CHECK(map != map_t{});
        CHECK(map != Expression{map_t{}});
        CHECK(map != list);
        CHECK(map != *list);
        CHECK(map != Expression::FromJson(R"(["3",1])"_json));

        // compare nullptr != null != false != 0 != "" != [] != {}
        auto exprs = std::vector<ExpressionPtr>{
            ExpressionPtr{nullptr},
            ExpressionPtr{ArtifactDescription::CreateTree(path{""})},
            ExpressionPtr{result_t{}},
            Expression::FromJson("null"_json),
            Expression::FromJson("false"_json),
            Expression::FromJson("0"_json),
            Expression::FromJson(R"("")"_json),
            Expression::FromJson("[]"_json),
            Expression::FromJson("{}"_json)};
        for (auto const& l : exprs) {
            for (auto const& r : exprs) {
                if (&l != &r) {
                    CHECK(l != r);
                }
            }
        }
    }

    SECTION("Throwing access operator") {
        // operators with argument of type size_t expect list
        CHECK_THROWS_AS(none[0], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(boolean[0], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(number[0], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(string[0], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(artifact[0], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(result[0], Expression::ExpressionTypeError);
        CHECK(list[0] == number);
        CHECK_THROWS_AS(map[0], Expression::ExpressionTypeError);

        // operators with argument of type std::string expect map
        CHECK_THROWS_AS(none["3"], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(boolean["3"], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(number["3"], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(string["3"], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(artifact["3"], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(result["3"], Expression::ExpressionTypeError);
        CHECK_THROWS_AS(list["3"], Expression::ExpressionTypeError);
        CHECK(map["3"] == number);
    }
}

TEST_CASE("Expression from JSON", "[expression]") {
    auto none = Expression::FromJson("null"_json);
    REQUIRE(none);
    CHECK(none->IsNone());

    auto boolean = Expression::FromJson("true"_json);
    REQUIRE(boolean);
    REQUIRE(boolean->IsBool());
    CHECK(boolean->Bool() == true);

    auto number = Expression::FromJson("1"_json);
    REQUIRE(number);
    REQUIRE(number->IsNumber());
    CHECK(number->Number() == 1);

    auto string = Expression::FromJson(R"("foo")"_json);
    REQUIRE(string);
    REQUIRE(string->IsString());
    CHECK(string->String() == "foo");

    auto list = Expression::FromJson("[]"_json);
    REQUIRE(list);
    REQUIRE(list->IsList());
    CHECK(list->List().empty());

    auto map = Expression::FromJson("{}"_json);
    REQUIRE(map);
    REQUIRE(map->IsMap());
    CHECK(map->Map().empty());
}

namespace {
auto TestToJson(nlohmann::json const& json) -> void {
    auto expr = Expression::FromJson(json);
    REQUIRE(expr);
    CHECK(expr->ToJson() == json);
}
}  // namespace

TEST_CASE("Expression to JSON", "[expression]") {
    TestToJson("null"_json);
    TestToJson("true"_json);
    TestToJson("1"_json);
    TestToJson(R"("foo")"_json);
    TestToJson("[]"_json);
    TestToJson("{}"_json);
}

namespace {
template <class T>
concept ValidExpressionTypeOrPtr =
    Expression::IsValidType<T>() or std::is_same_v<T, ExpressionPtr>;

template <ValidExpressionTypeOrPtr T>
auto Add(ExpressionPtr const& expr,
         std::string const& key,
         T const& by) -> ExpressionPtr {
    try {
        auto new_map = Expression::map_t::underlying_map_t{};
        new_map.emplace(key, by);
        return ExpressionPtr{Expression::map_t{expr, new_map}};
    } catch (...) {
        return ExpressionPtr{nullptr};
    }
}

template <ValidExpressionTypeOrPtr T>
auto Replace(ExpressionPtr const& expr,
             std::string const& key,
             T const& by) -> ExpressionPtr {
    auto const& map = expr->Map();
    if (not map.contains(key)) {
        return ExpressionPtr{nullptr};
    }
    return Add(expr, key, by);
}
}  // namespace

TEST_CASE("Expression Evaluation", "[expression]") {  // NOLINT
    using namespace std::string_literals;
    using number_t = Expression::number_t;
    using list_t = Expression::list_t;

    auto env = Configuration{};
    auto fcts = FunctionMapPtr{};

    auto foo = ExpressionPtr{"foo"s};
    auto bar = ExpressionPtr{"bar"s};
    auto baz = ExpressionPtr{"baz"s};

    SECTION("list object") {
        auto expr = Expression::FromJson(R"(["foo", "bar", "baz"])"_json);
        REQUIRE(expr);
        REQUIRE(expr->IsList());
        CHECK(expr->List().size() == 3);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result->List().size() == 3);
        CHECK(*result == *expr);
    }

    SECTION("map object without type") {
        auto expr = Expression::FromJson(R"({"foo": "bar"})"_json);
        REQUIRE(expr);
        auto result = expr.Evaluate(env, fcts);
        CHECK_FALSE(result);
    }

    SECTION("custom function") {
        auto expr = Expression::FromJson(R"(
            { "type": "'"
            , "$1": "PLACEHOLDER" })"_json);
        REQUIRE(expr);

        auto literal = Expression::FromJson(R"({"foo": "bar"})"_json);
        REQUIRE(literal);

        expr = Replace(expr, "$1", literal);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        CHECK(result);
        CHECK(*result == *literal);
    }

    SECTION("var expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "var"
            , "name": "foo" })"_json);
        REQUIRE(expr);

        auto none_result = expr.Evaluate(env, fcts);
        CHECK(none_result == Expression::FromJson(R"(null)"_json));

        env = env.Update("foo", "bar"s);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsString());
        CHECK(result == Expression::FromJson(R"("bar")"_json));

        auto overwrite = expr.Evaluate(env.Update("foo", list_t{result}), fcts);
        REQUIRE(overwrite);
        REQUIRE(overwrite->IsList());
        CHECK(overwrite == Expression::FromJson(R"(["bar"])"_json));
    }

    SECTION("quote expression") {
        auto expr = Expression::FromJson(R"(
          {"type": "'", "$1": {"type": "var", "name": "this is literal"}}
        )"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        CHECK(result == Expression::FromJson(R"(
          {"type": "var", "name": "this is literal"})"_json));

        auto expr_empty = Expression::FromJson(R"({"type": "'"})"_json);
        REQUIRE(expr_empty);
        auto result_empty = expr_empty.Evaluate(env, fcts);
        CHECK(result_empty == Expression::FromJson(R"(null)"_json));
    }

    SECTION("quasi-quote expression") {
        auto expr = Expression::FromJson(R"({"type": "`", "$1":
          { "foo": {"type": ",", "$1": {"type": "var", "name": "foo_var"}}
          , "bar": [ 1, 2, ["deep", "literals"]
                   , {"type": ",@", "$1": {"type": "var", "name": "bar_var"}}
                   , 3
                   , {"type": ",", "$1": {"type": "var", "name": "bar_plain"}}
                   , 4, 5
                   , {"type": ",", "$1": {"type": "var", "name": "foo_var"}}
                   , [ "deep", "expansion"
                    , {"type": ",", "$1": {"type": "var", "name": "bar_plain"}}
                    , {"type": ",@", "$1": {"type": "var", "name": "bar_var"}}
                    , {"type": ",", "$1": {"type": "var", "name": "foo_var"}}
                    ]
                   ]
          }
        })"_json);
        REQUIRE(expr);
        env = env.Update("foo_var", "foo value"s);
        env = env.Update("bar_var",
                         Expression::FromJson(R"(["b", "a", "r"])"_json));
        env =
            env.Update("bar_plain",
                       Expression::FromJson(R"(["kept", "as", "list"])"_json));
        auto result = expr.Evaluate(env, fcts);
        auto expected = Expression::FromJson(R"(
          { "foo": "foo value"
          , "bar": [ 1, 2, ["deep", "literals"]
                   , "b", "a", "r"
                   , 3
                   , ["kept", "as", "list"]
                   , 4, 5
                   , "foo value"
                   , [ "deep", "expansion"
                     , ["kept", "as", "list"]
                     , "b", "a", "r"
                     , "foo value"
                     ]
                   ]
         })"_json);

        CHECK(result == expected);

        auto doc_example_a = Expression::FromJson(
            R"({"type": "`", "$1": [1, 2, {"type": ",@", "$1": [3, 4]}]})"_json);
        auto result_a = doc_example_a.Evaluate(env, fcts);
        CHECK(result_a == Expression::FromJson(R"([1, 2, 3, 4])"_json));

        auto doc_example_b = Expression::FromJson(
            R"({"type": "`", "$1": [1, 2, {"type": ",", "$1": [3, 4]}]})"_json);
        auto result_b = doc_example_b.Evaluate(env, fcts);
        CHECK(result_b == Expression::FromJson(R"([1, 2, [3, 4]])"_json));
    }

    SECTION("if expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "if"
            , "cond": "PLACEHOLDER"
            , "then": "success"
            , "else": "failure" })"_json);
        REQUIRE(expr);

        SECTION("Boolean condition") {
            expr = Replace(expr, "cond", true);
            REQUIRE(expr);
            auto success = expr.Evaluate(env, fcts);
            REQUIRE(success);
            REQUIRE(success->IsString());
            CHECK(success == Expression::FromJson(R"("success")"_json));

            expr = Replace(expr, "cond", false);
            REQUIRE(expr);
            auto failure = expr.Evaluate(env, fcts);
            REQUIRE(failure);
            REQUIRE(failure->IsString());
            CHECK(failure == Expression::FromJson(R"("failure")"_json));
        }

        SECTION("Number condition") {
            expr = Replace(expr, "cond", number_t{1});
            REQUIRE(expr);
            auto success = expr.Evaluate(env, fcts);
            REQUIRE(success);
            REQUIRE(success->IsString());
            CHECK(success == Expression::FromJson(R"("success")"_json));

            expr = Replace(expr, "cond", number_t{0});
            REQUIRE(expr);
            auto failure = expr.Evaluate(env, fcts);
            REQUIRE(failure);
            REQUIRE(failure->IsString());
            CHECK(failure == Expression::FromJson(R"("failure")"_json));
        }

        SECTION("String condition") {
            expr = Replace(expr, "cond", "false"s);
            REQUIRE(expr);
            auto success = expr.Evaluate(env, fcts);
            REQUIRE(success);
            REQUIRE(success->IsString());
            CHECK(success == Expression::FromJson(R"("success")"_json));

            expr = Replace(expr, "cond", ""s);
            REQUIRE(expr);
            auto fail1 = expr.Evaluate(env, fcts);
            REQUIRE(fail1);
            REQUIRE(fail1->IsString());
            CHECK(fail1 == Expression::FromJson(R"("failure")"_json));
        }

        SECTION("List condition") {
            expr = Replace(expr, "cond", list_t{ExpressionPtr{}});
            REQUIRE(expr);
            auto success = expr.Evaluate(env, fcts);
            REQUIRE(success);
            REQUIRE(success->IsString());
            CHECK(success == Expression::FromJson(R"("success")"_json));

            expr = Replace(expr, "cond", list_t{});
            REQUIRE(expr);
            auto failure = expr.Evaluate(env, fcts);
            REQUIRE(failure);
            REQUIRE(failure->IsString());
            CHECK(failure == Expression::FromJson(R"("failure")"_json));
        }

        SECTION("Map condition") {
            auto literal = Expression::FromJson(
                R"({"type": "'", "$1": {"foo": "bar"}})"_json);
            REQUIRE(literal);
            expr = Replace(expr, "cond", literal);
            REQUIRE(expr);
            auto success = expr.Evaluate(env, fcts);
            REQUIRE(success);
            REQUIRE(success->IsString());
            CHECK(success == Expression::FromJson(R"("success")"_json));

            auto empty =
                Expression::FromJson(R"({"type": "'", "$1": {}})"_json);
            REQUIRE(empty);
            expr = Replace(expr, "cond", empty);
            REQUIRE(expr);
            auto failure = expr.Evaluate(env, fcts);
            REQUIRE(failure);
            REQUIRE(failure->IsString());
            CHECK(failure == Expression::FromJson(R"("failure")"_json));
        }
    }

    SECTION("if defaults") {
        auto expr = Expression::FromJson(R"(
          { "type": "if"
          , "cond": {"type": "var", "name": "x"}
          }
          )"_json);
        CHECK(expr.Evaluate(env.Update("x", true), fcts) ==
              Expression::FromJson(R"([])"_json));
        CHECK(expr.Evaluate(env.Update("x", false), fcts) ==
              Expression::FromJson(R"([])"_json));
    }

    SECTION("cond expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "cond"
            , "cond":
              [ [ { "type": "=="
                  , "$1": {"type":"var", "name": "val", "default": ""}
                  , "$2": 0
                  }
                , "number"
                ]
              , [ { "type": "=="
                  , "$1": {"type":"var", "name": "val", "default": ""}
                  , "$2": "0"
                  }
                , "string"
                ]
              , [ { "type": "=="
                  , "$1": {"type":"var", "name": "val", "default": ""}
                  , "$2": false
                  }
                , "boolean"
                ]
              , [ {"type":"var", "name": "val", "default": ""}, "first" ]
              , [ {"type":"var", "name": "val", "default": ""}, "second" ]
              ]})"_json);
        REQUIRE(expr);

        auto number = expr.Evaluate(env.Update("val", 0.0), fcts);
        REQUIRE(number);
        REQUIRE(number->IsString());
        CHECK(number == Expression::FromJson(R"("number")"_json));

        auto string = expr.Evaluate(env.Update("val", "0"s), fcts);
        REQUIRE(string);
        REQUIRE(string->IsString());
        CHECK(string == Expression::FromJson(R"("string")"_json));

        auto boolean = expr.Evaluate(env.Update("val", false), fcts);
        REQUIRE(boolean);
        REQUIRE(boolean->IsString());
        CHECK(boolean == Expression::FromJson(R"("boolean")"_json));

        auto first = expr.Evaluate(env.Update("val", true), fcts);
        REQUIRE(first);
        REQUIRE(first->IsString());
        CHECK(first == Expression::FromJson(R"("first")"_json));

        auto default1 = expr.Evaluate(env, fcts);
        REQUIRE(default1);
        REQUIRE(default1->IsList());
        CHECK(default1 == Expression::FromJson(R"([])"_json));

        expr = Add(expr, "default", "default"s);
        auto default2 = expr.Evaluate(env, fcts);
        REQUIRE(default2);
        REQUIRE(default2->IsString());
        CHECK(default2 == Expression::FromJson(R"("default")"_json));
    }

    SECTION("case expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "case"
            , "expr": {"type": "var", "name": "val", "default": ""}
            , "case":
              { "foo": "FOO"
              , "bar": {"type": "var", "name": "bar", "default": "BAR"}
              }
            })"_json);
        REQUIRE(expr);

        auto foo = expr.Evaluate(env.Update("val", "foo"s), fcts);
        REQUIRE(foo);
        REQUIRE(foo->IsString());
        CHECK(foo == Expression::FromJson(R"("FOO")"_json));

        auto bar = expr.Evaluate(env.Update("val", "bar"s), fcts);
        REQUIRE(bar);
        REQUIRE(bar->IsString());
        CHECK(bar == Expression::FromJson(R"("BAR")"_json));

        auto default1 = expr.Evaluate(env, fcts);
        REQUIRE(default1);
        REQUIRE(default1->IsList());
        CHECK(default1 == Expression::FromJson(R"([])"_json));

        expr = Add(expr, "default", "default"s);
        auto default2 = expr.Evaluate(env, fcts);
        REQUIRE(default2);
        REQUIRE(default2->IsString());
        CHECK(default2 == Expression::FromJson(R"("default")"_json));
    }

    SECTION("case* expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "case*"
            , "expr": {"type": "var", "name": "val"}
            , "case":
              [ [false, "FOO"]
              , [ {"type": "var", "name": "bar", "default": null}
                , {"type": "var", "name": "bar", "default": "BAR"}
                ]
              , [0, {"type": "join", "$1": ["B", "A", "Z"]}]
              ]
            })"_json);
        REQUIRE(expr);

        auto foo = expr.Evaluate(env.Update("val", false), fcts);
        REQUIRE(foo);
        REQUIRE(foo->IsString());
        CHECK(foo == Expression::FromJson(R"("FOO")"_json));

        auto bar = expr.Evaluate(env, fcts);
        REQUIRE(bar);
        REQUIRE(bar->IsString());
        CHECK(bar == Expression::FromJson(R"("BAR")"_json));

        auto baz = expr.Evaluate(env.Update("val", 0.0), fcts);
        REQUIRE(baz);
        REQUIRE(baz->IsString());
        CHECK(baz == Expression::FromJson(R"("BAZ")"_json));

        auto default1 = expr.Evaluate(env.Update("val", ""s), fcts);
        REQUIRE(default1);
        REQUIRE(default1->IsList());
        CHECK(default1 == Expression::FromJson(R"([])"_json));

        expr = Add(expr, "default", "default"s);
        auto default2 = expr.Evaluate(env.Update("val", ""s), fcts);
        REQUIRE(default2);
        REQUIRE(default2->IsString());
        CHECK(default2 == Expression::FromJson(R"("default")"_json));
    }

    SECTION("== expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "=="
            , "$1": "foo"
            , "$2": "PLACEHOLDER"})"_json);
        REQUIRE(expr);

        expr = Replace(expr, "$2", "foo"s);
        REQUIRE(expr);
        auto success = expr.Evaluate(env, fcts);
        REQUIRE(success);
        REQUIRE(success->IsBool());
        CHECK(success == Expression::FromJson("true"_json));

        expr = Replace(expr, "$2", "bar"s);
        REQUIRE(expr);
        auto failure = expr.Evaluate(env, fcts);
        REQUIRE(failure);
        REQUIRE(failure->IsBool());
        CHECK(failure == Expression::FromJson("false"_json));
    }

    SECTION("not expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "not"
            , "$1": {"type": "var", "name": "x" }
            })"_json);
        REQUIRE(expr);

        CHECK(expr.Evaluate(env.Update("x", true), fcts) ==
              Expression::FromJson("false"_json));
        CHECK(expr.Evaluate(env.Update("x", false), fcts) ==
              Expression::FromJson("true"_json));
        CHECK(expr.Evaluate(env.Update("x", Expression::FromJson(R"([])"_json)),
                            fcts) == Expression::FromJson("true"_json));
        CHECK(expr.Evaluate(
                  env.Update("x", Expression::FromJson(R"(["a"])"_json)),
                  fcts) == Expression::FromJson("false"_json));
        CHECK(expr.Evaluate(env.Update("x", Expression::FromJson("null"_json)),
                            fcts) == Expression::FromJson("true"_json));
        CHECK(expr.Evaluate(env.Update("x", Expression::FromJson("0"_json)),
                            fcts) == Expression::FromJson("true"_json));
        CHECK(expr.Evaluate(env.Update("x", Expression::FromJson("1"_json)),
                            fcts) == Expression::FromJson("false"_json));
        CHECK(expr.Evaluate(env.Update("x", Expression::FromJson(R"("")"_json)),
                            fcts) == Expression::FromJson("true"_json));
        CHECK(expr.Evaluate(
                  env.Update("x", Expression::FromJson(R"("0")"_json)), fcts) ==
              Expression::FromJson("false"_json));
    }

    SECTION("and expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "and"
            , "$1": "PLACEHOLDER" })"_json);
        REQUIRE(expr);

        auto empty = ExpressionPtr{""s};

        expr = Replace(expr, "$1", list_t{foo, bar});
        REQUIRE(expr);
        auto success = expr.Evaluate(env, fcts);
        REQUIRE(success);
        REQUIRE(success->IsBool());
        CHECK(success == Expression::FromJson("true"_json));

        expr = Replace(expr, "$1", list_t{foo, empty});
        REQUIRE(expr);
        auto failure = expr.Evaluate(env, fcts);
        REQUIRE(failure);
        REQUIRE(failure->IsBool());
        CHECK(failure == Expression::FromJson("false"_json));

        // test evaluation of list elements
        expr = Replace(expr, "$1", list_t{foo, Expression::FromJson(R"(
            {"type": "'"
            , "$1": true})"_json)});
        REQUIRE(expr);
        auto evaluated = expr.Evaluate(env, fcts);
        REQUIRE(evaluated);
        REQUIRE(evaluated->IsBool());
        CHECK(evaluated == Expression::FromJson("true"_json));

        // test short-circuit evaluation of logical and (static list)
        auto static_list =
            R"([true, false, {"type": "fail", "msg": "failed"}])"_json;
        expr = Replace(expr, "$1", Expression::FromJson(static_list));
        REQUIRE(expr);
        auto static_result = expr.Evaluate(env, fcts);
        REQUIRE(static_result);
        REQUIRE(static_result->IsBool());
        CHECK(static_result == Expression::FromJson("false"_json));

        // test full evaluation of dynamic list (expression evaluating to list)
        auto dynamic_list =
            nlohmann::json{{"type", "context"}, {"$1", static_list}};
        expr = Replace(expr, "$1", Expression::FromJson(dynamic_list));
        REQUIRE(expr);
        auto dyn_result = expr.Evaluate(env, fcts);
        REQUIRE_FALSE(dyn_result);
    }

    SECTION("or expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "or"
            , "$1": "PLACEHOLDER" })"_json);
        REQUIRE(expr);

        auto empty = ExpressionPtr{""s};

        expr = Replace(expr, "$1", list_t{foo, bar});
        REQUIRE(expr);
        auto success = expr.Evaluate(env, fcts);
        REQUIRE(success);
        REQUIRE(success->IsBool());
        CHECK(success == Expression::FromJson("true"_json));

        expr = Replace(expr, "$1", list_t{foo, empty});
        REQUIRE(expr);
        auto failure = expr.Evaluate(env, fcts);
        REQUIRE(failure);
        REQUIRE(failure->IsBool());
        CHECK(failure == Expression::FromJson("true"_json));

        // test evaluation of list elements
        expr = Replace(expr, "$1", list_t{foo, Expression::FromJson(R"(
            {"type": "'"
            , "$1": true})"_json)});
        REQUIRE(expr);
        auto evaluated = expr.Evaluate(env, fcts);
        REQUIRE(evaluated);
        REQUIRE(evaluated->IsBool());
        CHECK(evaluated == Expression::FromJson("true"_json));

        // test short-circuit evaluation of logical or (static list)
        auto static_list =
            R"([false, true, {"type": "fail", "msg": "failed"}])"_json;
        expr = Replace(expr, "$1", Expression::FromJson(static_list));
        REQUIRE(expr);
        auto static_result = expr.Evaluate(env, fcts);
        REQUIRE(static_result);
        REQUIRE(static_result->IsBool());
        CHECK(static_result == Expression::FromJson("true"_json));

        // test full evaluation of dynamic list (expression evaluating to list)
        auto dynamic_list =
            nlohmann::json{{"type", "context"}, {"$1", static_list}};
        expr = Replace(expr, "$1", Expression::FromJson(dynamic_list));
        REQUIRE(expr);
        auto dyn_result = expr.Evaluate(env, fcts);
        REQUIRE_FALSE(dyn_result);
    }

    SECTION("++ expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "++"
            , "$1": [ ["foo"]
                    , ["bar", "baz"]]})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result == Expression::FromJson(R"(["foo", "bar", "baz"])"_json));
    }

    SECTION("+ expression") {
        auto expr_empty = Expression::FromJson(R"(
            { "type": "+"
            , "$1": []
            })"_json);
        REQUIRE(expr_empty);

        auto result_empty = expr_empty.Evaluate(env, fcts);
        REQUIRE(result_empty);
        REQUIRE(result_empty->IsNumber());
        CHECK(result_empty == Expression::FromJson(R"(0.0)"_json));

        auto expr = Expression::FromJson(R"(
            { "type": "+"
            , "$1": [2, 3, 7, -1]
            })"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsNumber());
        CHECK(result == Expression::FromJson(R"(11.0)"_json));
    }

    SECTION("* expression") {
        auto expr_empty = Expression::FromJson(R"(
            { "type": "*"
            , "$1": []
            })"_json);
        REQUIRE(expr_empty);

        auto result_empty = expr_empty.Evaluate(env, fcts);
        REQUIRE(result_empty);
        REQUIRE(result_empty->IsNumber());
        CHECK(result_empty == Expression::FromJson(R"(1.0)"_json));

        auto expr = Expression::FromJson(R"(
            { "type": "*"
            , "$1": [2, 3, 7, -1]
            })"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsNumber());
        CHECK(result == Expression::FromJson(R"(-42.0)"_json));
    }

    SECTION("nub_right expression") {
        auto expr = Expression::FromJson(R"(
             {"type": "nub_right"
             , "$1": ["-lfoo", "-lbar", "-lbaz", "-lbar"]
             })"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result ==
              Expression::FromJson(R"(["-lfoo", "-lbaz", "-lbar"])"_json));
    }

    SECTION("nub_right expression 2") {
        auto expr = Expression::FromJson(R"(
             {"type": "nub_right"
             , "$1":
               { "type": "++"
               , "$1":
                 [ ["libg.a"]
                 , ["libe.a", "libd.a", "libc.a", "liba.a", "libb.a"]
                 , ["libf.a", "libc.a", "libd.a", "libb.a", "liba.a"]
                 , ["libc.a", "liba.a", "libb.a"]
                 , ["libd.a", "libb.a", "liba.a"]
                 ]
               }
             })"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result == Expression::FromJson(R"(
             ["libg.a", "libe.a", "libf.a", "libc.a", "libd.a", "libb.a", "liba.a"]
             )"_json));
    }

    SECTION("nub_left expression") {
        auto expr = Expression::FromJson(R"(
             {"type": "nub_left"
             , "$1": ["a", "b", "b", "a", "c", "b", "a"]
             })"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result == Expression::FromJson(R"(["a", "b", "c"])"_json));
    }

    SECTION("change_ending") {
        auto expr = Expression::FromJson(R"(
            { "type": "change_ending"
            , "$1": "PLACEHOLDER"
            , "ending": "_suffix" })"_json);
        REQUIRE(expr);

        expr = Replace(expr, "$1", ""s);
        REQUIRE(expr);
        auto empty_path = expr.Evaluate(env, fcts);
        REQUIRE(empty_path);
        REQUIRE(empty_path->IsString());
        CHECK(empty_path == Expression::FromJson(R"("_suffix")"_json));

        expr = Replace(expr, "$1", ".rc"s);
        REQUIRE(expr);
        auto hidden_file = expr.Evaluate(env, fcts);
        REQUIRE(hidden_file);
        REQUIRE(hidden_file->IsString());
        CHECK(hidden_file == Expression::FromJson(R"(".rc_suffix")"_json));

        expr = Replace(expr, "$1", "/root/path/file.txt"s);
        REQUIRE(expr);
        auto full_path = expr.Evaluate(env, fcts);
        REQUIRE(full_path);
        REQUIRE(full_path->IsString());
        CHECK(full_path ==
              Expression::FromJson(R"("/root/path/file_suffix")"_json));
    }

    SECTION("basename") {
        auto expr = Expression::FromJson(R"(
            { "type": "basename"
            , "$1": "PLACEHOLDER"
            })"_json);
        REQUIRE(expr);

        expr = Replace(expr, "$1", "foo.c"s);
        REQUIRE(expr);
        auto plain_file = expr.Evaluate(env, fcts);
        REQUIRE(plain_file);
        REQUIRE(plain_file->IsString());
        CHECK(plain_file == Expression::FromJson(R"("foo.c")"_json));

        expr = Replace(expr, "$1", "/path/to/file.txt"s);
        REQUIRE(expr);
        auto stripped_path = expr.Evaluate(env, fcts);
        REQUIRE(stripped_path);
        REQUIRE(stripped_path->IsString());
        CHECK(stripped_path == Expression::FromJson(R"("file.txt")"_json));
    }

    SECTION("join") {
        auto expr = Expression::FromJson(R"(
            { "type": "join"
            , "$1": "PLACEHOLDER"
            , "separator": ";" })"_json);
        REQUIRE(expr);

        expr = Replace(expr, "$1", list_t{});
        REQUIRE(expr);
        auto empty = expr.Evaluate(env, fcts);
        REQUIRE(empty);
        REQUIRE(empty->IsString());
        CHECK(empty == Expression::FromJson(R"("")"_json));

        expr = Replace(expr, "$1", list_t{foo});
        REQUIRE(expr);
        auto single = expr.Evaluate(env, fcts);
        REQUIRE(single);
        REQUIRE(single->IsString());
        CHECK(single == Expression::FromJson(R"("foo")"_json));

        expr = Replace(expr, "$1", list_t{foo, bar, baz});
        REQUIRE(expr);
        auto multi = expr.Evaluate(env, fcts);
        REQUIRE(multi);
        REQUIRE(multi->IsString());
        CHECK(multi == Expression::FromJson(R"("foo;bar;baz")"_json));

        expr = Replace(expr, "$1", foo);
        REQUIRE(expr);
        auto string = expr.Evaluate(env, fcts);
        REQUIRE(string);
        REQUIRE(string->IsString());
        CHECK(string == Expression::FromJson(R"("foo")"_json));

        // only list of strings or string is allowed
        expr = Replace(expr, "$1", list_t{foo, ExpressionPtr{number_t{}}});
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));

        expr = Replace(expr, "$1", number_t{});
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));
    }

    SECTION("join_cmd expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "join_cmd"
            , "$1": ["foo", "bar's", "baz"]})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsString());
        CHECK(result ==
              Expression::FromJson(R"("'foo' 'bar'\\''s' 'baz'")"_json));
    }

    SECTION("escape_chars expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "escape_chars"
            , "$1": "escape me X"
            , "chars": "abcX"
            , "escape_prefix": "X"})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsString());
        CHECK(result == Expression::FromJson(R"("esXcXape me XX")"_json));
    }

    SECTION("enumerate expression") {
        auto expr = Expression::FromJson(R"(
          { "type": "enumerate"
          , "$1": ["foo", "bar", "baz"]
          }
        )"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        CHECK(result == Expression::FromJson(R"(
          { "0000000000": "foo"
          , "0000000001": "bar"
          , "0000000002": "baz"
          }
        )"_json));
    }

    SECTION("set expression") {
        auto expr = Expression::FromJson(R"(
          { "type": "set"
          , "$1": ["foo", "bar", "baz"]
          }
        )"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        CHECK(result == Expression::FromJson(R"(
          { "foo": true
          , "bar": true
          , "baz": true
          }
        )"_json));
    }

    SECTION("reverse expression") {
        auto expr = Expression::FromJson(R"(
          { "type": "reverse"
          , "$1": ["foo", "bar", "baz"]
          }
        )"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        CHECK(result == Expression::FromJson(R"(
           ["baz", "bar", "foo"]
        )"_json));
    }

    SECTION("length expression") {
        auto expr = Expression::FromJson(R"(
          { "type": "length"
          , "$1": ["foo", "bar", "baz"]
          }
        )"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        CHECK(result == Expression::FromJson("3"_json));
    }

    SECTION("keys expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "keys"
            , "$1": { "type": "'"
                    , "$1": { "foo": true
                            , "bar": false
                            , "baz": true }}})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result == Expression::FromJson(R"(["bar", "baz", "foo"])"_json));
    }

    SECTION("values expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "values"
            , "$1": { "type": "'"
                    , "$1": { "foo": true
                            , "bar": "foo"
                            , "baz": 1 }}})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result == Expression::FromJson(R"(["foo", 1, true])"_json));
    }

    SECTION("lookup expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "lookup"
            , "key": "PLACEHOLDER"
            , "map": { "type": "'"
                    , "$1": { "foo": true
                            , "bar": 1 }}})"_json);
        REQUIRE(expr);

        expr = Replace(expr, "key", "foo"s);
        REQUIRE(expr);
        auto result_foo = expr.Evaluate(env, fcts);
        REQUIRE(result_foo);
        CHECK(result_foo == Expression::FromJson("true"_json));

        expr = Replace(expr, "key", "bar"s);
        REQUIRE(expr);
        auto result_bar = expr.Evaluate(env, fcts);
        REQUIRE(result_bar);
        CHECK(result_bar == Expression::FromJson("1"_json));

        // key baz is missing
        expr = Replace(expr, "key", "baz"s);
        REQUIRE(expr);
        auto result_baz = expr.Evaluate(env, fcts);
        REQUIRE(result_baz);
        CHECK(result_baz == Expression::FromJson("null"_json));

        // map is not mapping
        expr = Replace(expr, "map", list_t{});
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));

        // key is not string
        expr = Replace(expr, "key", number_t{});
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));
    }

    SECTION("lookup with default") {
        auto expr = Expression::FromJson(R"(
            { "type": "lookup"
            , "key": "PLACEHOLDER"
            , "map": { "type": "'"
                     , "$1": { "foo": false
                             , "bar": 1
                             , "baz" : null}}
            , "default" : { "type" : "join"
                          , "separator": "x"
                          , "$1": ["a", "b"]}})"_json);
        REQUIRE(expr);

        // Key present (and false)
        expr = Replace(expr, "key", "foo"s);
        REQUIRE(expr);
        auto result_foo = expr.Evaluate(env, fcts);
        REQUIRE(result_foo);
        CHECK(result_foo == Expression::FromJson("false"_json));

        // Key present but value is null
        expr = Replace(expr, "key", "baz"s);
        REQUIRE(expr);
        auto result_baz = expr.Evaluate(env, fcts);
        REQUIRE(result_baz);
        CHECK(result_baz == Expression::FromJson(R"("axb")"_json));

        // Key not present
        expr = Replace(expr, "key", "missing"s);
        REQUIRE(expr);
        auto result_missing = expr.Evaluate(env, fcts);
        REQUIRE(result_missing);
        CHECK(result_missing == Expression::FromJson(R"("axb")"_json));
    }

    SECTION("array index") {
        auto expr = Expression::FromJson(R"(
            { "type": "[]"
            , "list": ["a", 101, "c", null, "e"]
            , "index": "PLACEHOLDER"
            , "default": "here be dragons"
            })"_json);
        REQUIRE(expr);

        // Index a number
        expr = Replace(expr, "index", Expression::FromJson("2"_json));
        REQUIRE(expr);
        auto num_result = expr.Evaluate(env, fcts);
        REQUIRE(num_result);
        CHECK(num_result == Expression::FromJson(R"("c")"_json));

        // Index a string
        expr = Replace(expr, "index", Expression::FromJson(R"("2")"_json));
        REQUIRE(expr);
        auto string_result = expr.Evaluate(env, fcts);
        REQUIRE(string_result);
        CHECK(string_result == Expression::FromJson(R"("c")"_json));

        // Index pointing to a null value
        expr = Replace(expr, "index", Expression::FromJson("3"_json));
        REQUIRE(expr);
        auto null_result = expr.Evaluate(env, fcts);
        REQUIRE(null_result);
        CHECK(null_result == Expression::FromJson("null"_json));

        // Index out of range
        expr = Replace(expr, "index", Expression::FromJson("5"_json));
        REQUIRE(expr);
        auto default_result = expr.Evaluate(env, fcts);
        REQUIRE(default_result);
        CHECK(default_result ==
              Expression::FromJson(R"("here be dragons")"_json));

        // Negative index, number
        expr = Replace(expr, "index", Expression::FromJson("-3"_json));
        REQUIRE(expr);
        auto neg_index_number = expr.Evaluate(env, fcts);
        REQUIRE(neg_index_number);
        CHECK(neg_index_number == Expression::FromJson(R"("c")"_json));

        // Negative index, extreme number
        expr = Replace(expr, "index", Expression::FromJson("-5"_json));
        REQUIRE(expr);
        auto neg_index_number_extreme = expr.Evaluate(env, fcts);
        REQUIRE(neg_index_number_extreme);
        CHECK(neg_index_number_extreme == Expression::FromJson(R"("a")"_json));

        // Negative index, string
        expr = Replace(expr, "index", Expression::FromJson(R"("-3")"_json));
        REQUIRE(expr);
        auto neg_index_string = expr.Evaluate(env, fcts);
        REQUIRE(neg_index_string);
        CHECK(neg_index_string == Expression::FromJson(R"("c")"_json));

        // Index out of range, other direction
        expr = Replace(expr, "index", Expression::FromJson("-6"_json));
        REQUIRE(expr);
        auto other_default_result = expr.Evaluate(env, fcts);
        REQUIRE(other_default_result);
        CHECK(other_default_result ==
              Expression::FromJson(R"("here be dragons")"_json));
    }

    SECTION("empty_map expression") {
        auto expr = Expression::FromJson(R"({"type": "empty_map"})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result == Expression::FromJson("{}"_json));
    }

    SECTION("singleton_map expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "singleton_map"
            , "key": "foo"
            , "value": "bar"})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result == Expression::FromJson(R"({"foo": "bar"})"_json));
    }

    SECTION("disjoint_map_union expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "disjoint_map_union"
            , "$1": "PLACEHOLDER" })"_json);
        REQUIRE(expr);

        auto literal_foo =
            Expression::FromJson(R"({"type": "'", "$1": {"foo":true}})"_json);
        REQUIRE(literal_foo);
        auto literal_foo_false =
            Expression::FromJson(R"({"type": "'", "$1": {"foo":false}})"_json);
        REQUIRE(literal_foo_false);
        auto literal_bar =
            Expression::FromJson(R"({"type": "'", "$1": {"bar":false}})"_json);
        REQUIRE(literal_bar);

        expr = Replace(expr, "$1", list_t{literal_foo, literal_bar});
        REQUIRE(expr);
        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result ==
              Expression::FromJson(R"({"foo": true, "bar": false})"_json));

        // duplicate foo, but with same value
        expr = Replace(expr, "$1", list_t{literal_foo, literal_foo});
        REQUIRE(expr);
        result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result == Expression::FromJson(R"({"foo": true})"_json));

        // duplicate foo, but with different value
        expr = Replace(expr, "$1", list_t{literal_foo, literal_foo_false});
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));

        // empty list should produce empty map
        expr = Replace(expr, "$1", list_t{});
        REQUIRE(expr);
        auto empty = expr.Evaluate(env, fcts);
        REQUIRE(empty);
        REQUIRE(empty->IsMap());
        REQUIRE(empty == Expression::FromJson("{}"_json));
    }

    SECTION("map_union expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "map_union"
            , "$1": { "type": "'"
                    , "$1": [ {"foo": true}
                            , {"bar": false}] }})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result ==
              Expression::FromJson(R"({"foo": true, "bar": false})"_json));

        // empty list should produce empty map
        expr = Expression::FromJson(R"({"type": "map_union", "$1": []})"_json);
        REQUIRE(expr);
        auto empty = expr.Evaluate(env, fcts);
        REQUIRE(empty);
        REQUIRE(empty->IsMap());
        REQUIRE(empty == Expression::FromJson("{}"_json));
    }

    SECTION("to_subdir expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "to_subdir"
            , "subdir": "prefix"
            , "$1": { "type": "'"
                    , "$1": { "foo": "hello"
                            , "bar": "world" }}})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result ==
              Expression::FromJson(
                  R"({"prefix/foo": "hello", "prefix/bar": "world"})"_json));
    }

    SECTION("to_subdir expression with conflict") {
        auto expr = Expression::FromJson(R"(
            { "type": "to_subdir"
            , "subdir": "prefix"
            , "$1": { "type": "'"
                    , "$1": { "foo": "hello"
                            , "./foo": "world" }}})"_json);
        REQUIRE(expr);

        CHECK_FALSE(expr.Evaluate(env, fcts));
    }

    SECTION("flat to_subdir without proper conflict") {
        auto expr = Expression::FromJson(R"(
            { "type": "to_subdir"
            , "subdir": "prefix"
            , "flat" : "YES"
            , "$1": { "type": "'"
                    , "$1": { "foobar/data/foo": "hello"
                            , "foobar/include/foo": "hello"
                            , "bar": "world" }}})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result ==
              Expression::FromJson(
                  R"({"prefix/foo": "hello", "prefix/bar": "world"})"_json));
    }

    SECTION("flat to_subdir with conflict") {
        auto expr = Expression::FromJson(R"(
            { "type": "to_subdir"
            , "subdir": "prefix"
            , "flat" : "YES"
            , "$1": { "type": "'"
                    , "$1": { "foobar/data/foo": "HELLO"
                            , "foobar/include/foo": "hello"
                            , "bar": "world" }}})"_json);
        REQUIRE(expr);

        CHECK_FALSE(expr.Evaluate(env, fcts));
    }

    SECTION("from_subdir") {
        auto expr = Expression::FromJson(R"(
       {"type": "from_subdir", "subdir": "foo"
       , "$1": {"type": "'", "$1":
          { "foo/a/b/c": "abc.txt"
          , "foo/a/other": "other.txt"
          , "foo/top": "top.xt"
          , "foo/a/b/../d/e": "make canonical"
          , "bar/a/b/c": "ignore bar/a/b/c"
          , "bar/a/b/../b/c": "also ingnore other path"
          }}}
      )"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        CHECK(result == Expression::FromJson(R"(
          { "a/b/c": "abc.txt"
          , "a/other": "other.txt"
          , "top": "top.xt"
          , "a/d/e": "make canonical"
          }
      )"_json));
    }

    SECTION("from_subdir trivial conflict") {
        auto expr = Expression::FromJson(R"(
      {"type": "from_subdir", "subdir": "foo"
       , "$1": {"type": "'", "$1":
          { "foo/a/b/c": "abc.txt"
          , "foo/a/b/../b/c": "abc.txt"
          }}}
      )"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        CHECK(result == Expression::FromJson(R"(
       {"a/b/c": "abc.txt"}
      )"_json));
    }

    SECTION("from_subdir conflict") {
        auto expr = Expression::FromJson(R"(
       {"type": "from_subdir", "subdir": "foo"
       , "$1": {"type": "'", "$1":
          { "foo/a/b/c": "one value"
          , "foo/a/b/../b/c": "different value"
          }}}
      )"_json);
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));
    }

    fcts = FunctionMap::MakePtr(
        fcts, "concat", [](auto&& eval, auto const& expr, auto const& env) {
            auto p1 = eval(expr->Get("$1", ""s), env);
            auto p2 = eval(expr->Get("$2", ""s), env);
            return ExpressionPtr{p1->String() + p2->String()};
        });

    SECTION("foreach expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "foreach"
            , "var": "x"
            , "range": ["foo", "bar", "baz"]
            , "body": { "type": "concat"
                      , "$1": { "type": "var"
                              , "name": "x" }
                      , "$2": "y" }})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result ==
              Expression::FromJson(R"(["fooy", "bary", "bazy"])"_json));
    }

    SECTION("foreach_map expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "foreach_map"
            , "var_key": "key"
            , "var_val": "val"
            , "body": { "type": "concat"
                      , "$1": { "type": "var"
                              , "name": "key" }
                      , "$2": { "type": "var"
                              , "name": "val" }}})"_json);
        REQUIRE(expr);

        // range is missing (should default to empty map)
        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result == Expression::FromJson(R"([])"_json));

        // range is map with one entry
        expr = Add(expr, "range", Expression::FromJson(R"(
            { "type": "'"
            , "$1": {"foo": "bar"}})"_json));
        REQUIRE(expr);

        result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result == Expression::FromJson(R"(["foobar"])"_json));

        // range is map with multiple entries
        expr = Replace(expr, "range", Expression::FromJson(R"(
            { "type": "'"
            , "$1": {"foo": "bar", "bar": "baz"}})"_json));
        REQUIRE(expr);

        result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsList());
        CHECK(result == Expression::FromJson(R"(["barbaz", "foobar"])"_json));

        // fail if range is string
        expr = Replace(expr, "range", Expression::FromJson(R"("foo")"_json));
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));

        // fail if range is number
        expr = Replace(expr, "range", Expression::FromJson(R"("4711")"_json));
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));

        // fail if range is Boolean
        expr = Replace(expr, "range", Expression::FromJson(R"("true")"_json));
        REQUIRE(expr);
        CHECK_FALSE(expr.Evaluate(env, fcts));
    }

    SECTION("foldl expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "foldl"
            , "var": "x"
            , "range": ["bar", "baz"]
            , "accum_var": "a"
            , "start": "foo"
            , "body": { "type": "concat"
                      , "$1": { "type": "var"
                              , "name": "x" }
                      , "$2": { "type": "var"
                              , "name": "a" }}})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsString());
        CHECK(result == Expression::FromJson(R"("bazbarfoo")"_json));
    }

    SECTION("let* expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "let*"
            , "bindings": [ ["foo", "foo"]
                          , ["bar", "bar"] ]
            , "body": { "type": "concat"
                      , "$1": { "type": "var"
                              , "name": "foo" }
                      , "$2": { "type": "var"
                              , "name": "bar" }}})"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsString());
        CHECK(result == Expression::FromJson(R"("foobar")"_json));
    }

    SECTION("sequentiallity of let* expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "let*"
            , "bindings":
              [ ["one", "foo"]
              , ["two", { "type": "join"
                        , "$1": [ {"type": "var", "name" : "one"}
                                , {"type": "var", "name" : "one"} ]}]
              , ["four", { "type": "join"
                         , "$1": [ {"type": "var", "name" : "two"}
                                 , {"type": "var", "name" : "two"} ]}]
              ]
            , "body": { "type" : "var"
                      , "name" : "four" }
            })"_json);
        REQUIRE(expr);

        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsString());
        CHECK(result == Expression::FromJson(R"("foofoofoofoo")"_json));
    }

    SECTION("env expression") {
        auto env = Configuration{Expression::FromJson(
            R"({"foo": "FOO_STRING", "bar": "BAR_STRING"})"_json)};

        auto expr = Expression::FromJson(R"(
            { "type": "env"
            , "vars": ["bar", "baz"]
            })"_json);
        REQUIRE(expr);
        auto result = expr.Evaluate(env, fcts);
        REQUIRE(result);
        REQUIRE(result->IsMap());
        CHECK(result == Expression::FromJson(
                            R"({"bar": "BAR_STRING", "baz": null})"_json));

        auto empty = Expression::FromJson(R"({"type": "env"})"_json);
        REQUIRE(empty);
        auto none = empty.Evaluate(env, fcts);
        REQUIRE(none);
        REQUIRE(none->IsMap());
        CHECK(none == Expression::kEmptyMap);
    }

    SECTION("concat_target_name expression") {
        auto expr = Expression::FromJson(R"(
            { "type": "concat_target_name"
            , "$1": "PLACEHOLDER"
            , "$2": "_suffix" })"_json);
        REQUIRE(expr);

        expr = Replace(expr, "$1", "foo"s);
        REQUIRE(expr);
        auto str_result = expr.Evaluate(env, fcts);
        REQUIRE(str_result);
        REQUIRE(str_result->IsString());
        CHECK(str_result == Expression::FromJson(R"("foo_suffix")"_json));

        auto dep_tgt = Expression::FromJson(R"(["subdir", "bar"])"_json);
        REQUIRE(dep_tgt);
        expr = Replace(expr, "$1", dep_tgt);
        REQUIRE(expr);
        auto dep_result = expr.Evaluate(env, fcts);
        REQUIRE(dep_result);
        REQUIRE(dep_result->IsList());
        CHECK(dep_result ==
              Expression::FromJson(R"(["subdir", "bar_suffix"])"_json));
    }

    SECTION("range expression") {
        auto expr_str = Expression::FromJson(R"(
            { "type": "range"
            , "$1": "3"
            })"_json);
        REQUIRE(expr_str);
        auto str_result = expr_str.Evaluate(env, fcts);
        CHECK(str_result == Expression::FromJson(R"(["0", "1", "2"])"_json));

        auto expr_number = Expression::FromJson(R"(
            { "type": "range"
            , "$1": 4
            })"_json);
        REQUIRE(expr_number);
        auto number_result = expr_number.Evaluate(env, fcts);
        CHECK(number_result ==
              Expression::FromJson(R"(["0", "1", "2", "3"])"_json));

        auto expr_null = Expression::FromJson(R"(
            { "type": "range"
            , "$1": null
            })"_json);
        REQUIRE(expr_null);
        auto null_result = expr_null.Evaluate(env, fcts);
        CHECK(null_result == Expression::FromJson(R"([])"_json));
    }
}

TEST_CASE("Expression Assertions", "[expression]") {
    using namespace std::string_literals;
    auto env = Configuration{};
    auto fcts = FunctionMapPtr{};

    SECTION("fail") {
        auto expr = Expression::FromJson(R"(
         { "type": "fail"
         , "msg": {"type": "join", "$1": ["ErRoR", "mEsSaGe"]}
         }
         )"_json);
        REQUIRE(expr);

        std::stringstream log{};
        CHECK(not expr.Evaluate(env, fcts, [&](auto msg) { log << msg; }));
        CHECK(log.str().find("ErRoRmEsSaGe") != std::string::npos);
    }

    SECTION("assert_non_empty") {
        auto expr = Expression::FromJson(R"(
           { "type": "assert_non_empty"
           , "msg": "Found-Empty!!"
           , "$1": {"type": "var", "name": "x"}
           }
           )"_json);
        REQUIRE(expr);

        auto list = Expression::FromJson(R"([1, 2, 3])"_json);
        CHECK(expr.Evaluate(env.Update("x", list), fcts) == list);
        auto map = Expression::FromJson(R"({"foo": "bar"})"_json);
        CHECK(expr.Evaluate(env.Update("x", map), fcts) == map);

        auto empty_list = Expression::FromJson(R"([])"_json);
        std::stringstream log_list{};
        CHECK(not expr.Evaluate(env.Update("x", empty_list),
                                fcts,
                                [&](auto msg) { log_list << msg; }));
        CHECK(log_list.str().find("Found-Empty!!") != std::string::npos);

        auto empty_map = Expression::FromJson(R"({})"_json);
        std::stringstream log_map{};
        CHECK(not expr.Evaluate(env.Update("x", empty_map),
                                fcts,
                                [&](auto msg) { log_map << msg; }));
        CHECK(log_map.str().find("Found-Empty!!") != std::string::npos);
    }

    SECTION("assert") {
        auto expr = Expression::FromJson(R"(
           { "type": "assert"
           , "predicate": {"type": "[]", "index": 0
                           , "list": {"type": "var", "name": "_"}}
           , "msg": ["First entry UNTRUE", {"type": "var", "name": "_"}]
           , "$1": {"type": "++", "$1": [{"type": "var", "name": "x"}
                                        , ["b", "c"]]}
           })"_json);
        REQUIRE(expr);

        CHECK(expr.Evaluate(
                  env.Update("x", Expression::FromJson(R"(["a"])"_json)),
                  fcts) == Expression::FromJson(R"(["a", "b", "c"])"_json));

        std::stringstream log{};
        CHECK(not expr.Evaluate(
            env.Update("x", Expression::FromJson(R"([false, "foo"])"_json)),
            fcts,
            [&](auto msg) { log << msg; }));
        // log must contain the canoncial (minimal) repesentation of evaluating
        // "msg"
        CHECK(
            log.str().find(R"(["First entry UNTRUE",[false,"foo","b","c"])"s) !=
            std::string::npos);
    }
}

TEST_CASE("Expression hash computation", "[expression]") {
    using namespace std::string_literals;
    using path = std::filesystem::path;
    using number_t = Expression::number_t;
    using artifact_t = Expression::artifact_t;
    using result_t = Expression::result_t;
    using list_t = Expression::list_t;
    using map_t = Expression::map_t;

    auto none = ExpressionPtr{};
    auto boolean = ExpressionPtr{false};
    auto number = ExpressionPtr{number_t{}};
    auto string = ExpressionPtr{""s};
    auto artifact = ExpressionPtr{ArtifactDescription::CreateTree(path{""})};
    auto result = ExpressionPtr{result_t{}};
    auto list = ExpressionPtr{list_t{}};
    auto map = ExpressionPtr{map_t{}};

    CHECK_FALSE(none->ToHash().empty());
    CHECK(none->ToHash() == Expression{}.ToHash());

    CHECK_FALSE(boolean->ToHash().empty());
    CHECK(boolean->ToHash() == Expression{false}.ToHash());
    CHECK_FALSE(boolean->ToHash() == Expression{true}.ToHash());

    CHECK_FALSE(number->ToHash().empty());
    CHECK(number->ToHash() == Expression{number_t{}}.ToHash());
    CHECK_FALSE(number->ToHash() == Expression{number_t{1}}.ToHash());

    CHECK_FALSE(string->ToHash().empty());
    CHECK(string->ToHash() == Expression{""s}.ToHash());
    CHECK_FALSE(string->ToHash() == Expression{" "s}.ToHash());

    CHECK_FALSE(artifact->ToHash().empty());
    CHECK(artifact->ToHash() ==
          Expression{ArtifactDescription::CreateTree(path{""})}.ToHash());
    CHECK_FALSE(
        artifact->ToHash() ==
        Expression{ArtifactDescription::CreateTree(path{" "})}.ToHash());

    CHECK_FALSE(result->ToHash().empty());
    CHECK(result->ToHash() == Expression{result_t{}}.ToHash());
    CHECK_FALSE(result->ToHash() == Expression{result_t{boolean}}.ToHash());

    CHECK_FALSE(list->ToHash().empty());
    CHECK(list->ToHash() == Expression{list_t{}}.ToHash());
    CHECK_FALSE(list->ToHash() == Expression{list_t{number}}.ToHash());
    CHECK_FALSE(list->ToHash() == Expression{map_t{{""s, number}}}.ToHash());

    CHECK_FALSE(map->ToHash().empty());
    CHECK(map->ToHash() == Expression{map_t{}}.ToHash());
    CHECK_FALSE(map->ToHash() == Expression{map_t{{""s, number}}}.ToHash());
    CHECK_FALSE(map->ToHash() == Expression{list_t{string, number}}.ToHash());

    auto exprs = std::vector<ExpressionPtr>{
        none, boolean, number, string, artifact, result, list, map};
    for (auto const& l : exprs) {
        for (auto const& r : exprs) {
            if (&l != &r) {
                CHECK_FALSE(l->ToHash() == r->ToHash());
            }
        }
    }
}
