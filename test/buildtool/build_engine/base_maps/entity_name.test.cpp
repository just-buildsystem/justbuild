#include "catch2/catch.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"

TEST_CASE("Normal module names") {
    using EN = BuildMaps::Base::EntityName;
    CHECK(EN::normal_module_name("foo/bar") == "foo/bar");
    CHECK(EN::normal_module_name("foo/bar/") == "foo/bar");
    CHECK(EN::normal_module_name("./foo/bar") == "foo/bar");
    CHECK(EN::normal_module_name("/foo/bar") == "foo/bar");
    CHECK(EN::normal_module_name("/foo/bar/.") == "foo/bar");
    CHECK(EN::normal_module_name("/foo/bar/baz/..") == "foo/bar");
    CHECK(EN::normal_module_name("foo/baz/../bar") == "foo/bar");
    CHECK(EN::normal_module_name("../../../foo/bar") == "foo/bar");

    CHECK(EN::normal_module_name("").empty());
    CHECK(EN::normal_module_name(".").empty());
    CHECK(EN::normal_module_name("./").empty());
    CHECK(EN::normal_module_name("./.").empty());
    CHECK(EN::normal_module_name("/").empty());
    CHECK(EN::normal_module_name("/.").empty());
    CHECK(EN::normal_module_name("..").empty());
}
