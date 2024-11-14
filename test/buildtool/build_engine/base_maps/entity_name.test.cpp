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

#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"

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
