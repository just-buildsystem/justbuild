#include "catch2/catch.hpp"
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
