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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

namespace {

[[nodiscard]] auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/build_engine/target_map";
}

[[nodiscard]] auto CreateAnalysedTarget(
    TargetResult const& result,
    std::vector<ActionDescription::Ptr> const& descs,
    std::vector<std::string> const& blobs) -> AnalysedTargetPtr {
    return std::make_shared<AnalysedTarget const>(
        result,
        descs,
        blobs,
        std::vector<Tree::Ptr>(),
        std::unordered_set<std::string>{},
        std::set<std::string>{},
        TargetGraphInformation::kSource);
}

}  // namespace

TEST_CASE("empty map", "[result_map]") {
    using BuildMaps::Target::ResultTargetMap;
    ResultTargetMap map{0};

    CHECK(map.ToResult().actions.empty());
    CHECK(map.ToResult().blobs.empty());

    CHECK(map.ToJson() == R"({"actions": {}, "blobs": [], "trees": {}})"_json);

    auto filename = (GetTestDir() / "test_empty.graph").string();
    map.ToFile(filename);
    std::ifstream file(filename);
    nlohmann::json from_file{};
    file >> from_file;
    CHECK(from_file == R"({"actions": {}, "blobs": [], "trees": {}})"_json);
}

TEST_CASE("origins creation", "[result_map]") {
    using BuildMaps::Base::EntityName;
    using BuildMaps::Target::ResultTargetMap;

    auto foo = std::make_shared<ActionDescription>(
        ActionDescription::outputs_t{},
        ActionDescription::outputs_t{},
        Action{"run_foo", {"touch", "foo"}, {}},
        ActionDescription::inputs_t{});
    auto bar = std::make_shared<ActionDescription>(
        ActionDescription::outputs_t{},
        ActionDescription::outputs_t{},
        Action{"run_bar", {"touch", "bar"}, {}},
        ActionDescription::inputs_t{});
    auto baz = std::make_shared<ActionDescription>(
        ActionDescription::outputs_t{},
        ActionDescription::outputs_t{},
        Action{"run_baz", {"touch", "baz"}, {}},
        ActionDescription::inputs_t{});

    ResultTargetMap map{0};
    CHECK(map.Add(EntityName{"", ".", "foobar"},
                  {},
                  CreateAnalysedTarget(
                      {}, std::vector<ActionDescription::Ptr>{foo, bar}, {})));
    CHECK(map.Add(EntityName{"", ".", "baz"},
                  {},
                  CreateAnalysedTarget(
                      {}, std::vector<ActionDescription::Ptr>{baz}, {})));

    auto result = map.ToResult();
    REQUIRE(result.actions.size() == 3);
    CHECK(result.blobs.empty());

    auto expect_foo = foo->ToJson();
    auto expect_bar = bar->ToJson();
    auto expect_baz = baz->ToJson();
    CHECK(map.ToJson() == nlohmann::json{{"actions",
                                          {{foo->Id(), expect_foo},
                                           {bar->Id(), expect_bar},
                                           {baz->Id(), expect_baz}}},
                                         {"blobs", nlohmann::json::array()},
                                         {"trees", nlohmann::json::object()}});

    expect_foo["origins"] =
        R"([{"target": ["@", "", "", "foobar"], "config": {}, "subtask":
        0}])"_json;
    expect_bar["origins"] =
        R"([{"target": ["@", "", "", "foobar"], "config": {}, "subtask":
        1}])"_json;
    expect_baz["origins"] =
        R"([{"target": ["@", "", "", "baz"], "config": {}, "subtask":
        0}])"_json;

    auto filename = (GetTestDir() / "test_with_origins.graph").string();
    map.ToFile(filename);
    std::ifstream file(filename);
    nlohmann::json from_file{};
    file >> from_file;
    CHECK(from_file == nlohmann::json{{"actions",
                                       {{foo->Id(), expect_foo},
                                        {bar->Id(), expect_bar},
                                        {baz->Id(), expect_baz}}},
                                      {"blobs", nlohmann::json::array()},
                                      {"trees", nlohmann::json::object()}});
}

TEST_CASE("blobs uniqueness", "[result_map]") {
    using BuildMaps::Base::EntityName;
    using BuildMaps::Target::ResultTargetMap;

    ResultTargetMap map{0};
    CHECK(map.Add(EntityName{"", ".", "foobar"},
                  {},
                  CreateAnalysedTarget({}, {}, {"foo", "bar"})));
    CHECK(map.Add(EntityName{"", ".", "barbaz"},
                  {},
                  CreateAnalysedTarget({}, {}, {"bar", "baz"})));

    auto result = map.ToResult();
    CHECK(result.actions.empty());
    CHECK(result.blobs.size() == 3);

    CHECK(map.ToJson() == nlohmann::json{{"actions", nlohmann::json::object()},
                                         {"blobs", {"bar", "baz", "foo"}},
                                         {"trees", nlohmann::json::object()}});

    auto filename = (GetTestDir() / "test_unique_blobs.graph").string();
    map.ToFile</*kIncludeOrigins=*/false>(filename);
    std::ifstream file(filename);
    nlohmann::json from_file{};
    file >> from_file;
    CHECK(from_file == nlohmann::json{{"actions", nlohmann::json::object()},
                                      {"blobs", {"bar", "baz", "foo"}},
                                      {"trees", nlohmann::json::object()}});
}
