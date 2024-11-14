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

#include "src/buildtool/common/action_description.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/action.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"

TEST_CASE("From JSON", "[action_description]") {
    using path = std::filesystem::path;
    auto desc = ActionDescription{
        {"output0", "output1"},
        {"dir0", "dir1"},
        Action{"id", {"command", "line"}, {{"env", "vars"}}},
        {{"path0", ArtifactDescription::CreateTree(path{"input0"})},
         {"path1", ArtifactDescription::CreateTree(path{"input1"})}}};
    auto const& action = desc.GraphAction();
    auto json =
        ActionDescription{desc.OutputFiles(),
                          desc.OutputDirs(),
                          Action{"unused", action.Command(), action.Env()},
                          desc.Inputs()}
            .ToJson();

    auto const hash_type = TestHashType::ReadFromEnvironment();
    SECTION("Parse full action") {
        auto description = ActionDescription::FromJson(hash_type, "id", json);
        REQUIRE(description);
        CHECK((*description)->ToJson() == json);
    }

    SECTION("Parse action without optional input") {
        json["input"] = nlohmann::json::object();
        CHECK(ActionDescription::FromJson(hash_type, "id", json));

        json["input"] = nlohmann::json::array();
        CHECK_FALSE(ActionDescription::FromJson(hash_type, "id", json));

        json.erase("input");
        CHECK(ActionDescription::FromJson(hash_type, "id", json));
    }

    SECTION("Parse action without optional env") {
        json["env"] = nlohmann::json::object();
        CHECK(ActionDescription::FromJson(hash_type, "id", json));

        json["env"] = nlohmann::json::array();
        CHECK_FALSE(ActionDescription::FromJson(hash_type, "id", json));

        json.erase("env");
        CHECK(ActionDescription::FromJson(hash_type, "id", json));
    }

    SECTION("Parse action without mandatory outputs") {
        json["output"] = nlohmann::json::array();
        json["output_dirs"] = nlohmann::json::array();
        CHECK_FALSE(ActionDescription::FromJson(hash_type, "id", json));

        json["output"] = nlohmann::json::object();
        json["output_dirs"] = nlohmann::json::object();
        CHECK_FALSE(ActionDescription::FromJson(hash_type, "id", json));

        json.erase("output");
        json.erase("output_dirs");
        CHECK_FALSE(ActionDescription::FromJson(hash_type, "id", json));
    }

    SECTION("Parse action without mandatory command") {
        json["command"] = nlohmann::json::array();
        CHECK_FALSE(ActionDescription::FromJson(hash_type, "id", json));

        json["command"] = nlohmann::json::object();
        CHECK_FALSE(ActionDescription::FromJson(hash_type, "id", json));

        json.erase("command");
        CHECK_FALSE(ActionDescription::FromJson(hash_type, "id", json));
    }
}
