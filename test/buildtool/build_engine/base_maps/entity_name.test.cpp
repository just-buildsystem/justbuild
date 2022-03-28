#include "catch2/catch.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"

TEST_CASE("Normal module names") {
    using NT = BuildMaps::Base::NamedTarget;
    CHECK(NT::normal_module_name("foo/bar") == "foo/bar");
    CHECK(NT::normal_module_name("foo/bar/") == "foo/bar");
    CHECK(NT::normal_module_name("./foo/bar") == "foo/bar");
    CHECK(NT::normal_module_name("/foo/bar") == "foo/bar");
    CHECK(NT::normal_module_name("/foo/bar/.") == "foo/bar");
    CHECK(NT::normal_module_name("/foo/bar/baz/..") == "foo/bar");
    CHECK(NT::normal_module_name("foo/baz/../bar") == "foo/bar");
    CHECK(NT::normal_module_name("../../../foo/bar") == "foo/bar");

    CHECK(NT::normal_module_name("").empty());
    CHECK(NT::normal_module_name(".").empty());
    CHECK(NT::normal_module_name("./").empty());
    CHECK(NT::normal_module_name("./.").empty());
    CHECK(NT::normal_module_name("/").empty());
    CHECK(NT::normal_module_name("/.").empty());
    CHECK(NT::normal_module_name("..").empty());
}
