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
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/target_map/utils.hpp"

TEST_CASE("Tree conflicts", "[tree_conflict]") {
    auto overlap = Expression::FromJson(R"({ "foo/bar": "content-1"
                            , "foo/bar/baz": "content-2"})"_json);
    REQUIRE(overlap);
    auto overlap_conflict = BuildMaps::Target::Utils::tree_conflict(overlap);
    CHECK(overlap_conflict);
    CHECK(*overlap_conflict == "foo/bar");

    auto root_overlap = Expression::FromJson(R"({ ".": "content-1"
                            , "foo": "content-2"})"_json);
    REQUIRE(root_overlap);
    auto root_overlap_conflict =
        BuildMaps::Target::Utils::tree_conflict(root_overlap);
    CHECK(root_overlap_conflict);
    CHECK(*root_overlap_conflict == ".");

    auto upwards_reference = Expression::FromJson(R"(
      { "../foo.txt" : "content" })"_json);
    REQUIRE(upwards_reference);
    auto upwards_reference_conflict =
        BuildMaps::Target::Utils::tree_conflict(upwards_reference);
    CHECK(upwards_reference_conflict);
    CHECK(*upwards_reference_conflict == "../foo.txt");

    auto absolute_reference = Expression::FromJson(R"(
      { "/foo.txt" : "content" })"_json);
    REQUIRE(absolute_reference);
    auto absolute_reference_conflict =
        BuildMaps::Target::Utils::tree_conflict(absolute_reference);
    CHECK(absolute_reference_conflict);
    CHECK(*absolute_reference_conflict == "/foo.txt");
}

TEST_CASE("No conflict", "[tree_conflict]") {
    auto no_overlap = Expression::FromJson(R"({ "foo/bar/baz.txt": "content-1"
                            , "foo/bar/baz": "content-2"})"_json);
    REQUIRE(no_overlap);
    auto no_overlap_conflict =
        BuildMaps::Target::Utils::tree_conflict(no_overlap);
    CHECK(!no_overlap_conflict);

    auto single_root = Expression::FromJson(R"({ ".": "content-1"})"_json);
    REQUIRE(single_root);
    auto single_root_conflict =
        BuildMaps::Target::Utils::tree_conflict(single_root);
    CHECK(!single_root_conflict);
}
