#include "catch2/catch.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact_factory.hpp"

TEST_CASE("From JSON", "[action_description]") {
    using path = std::filesystem::path;
    auto desc =
        ActionDescription{{"output0", "output1"},
                          {"dir0", "dir1"},
                          Action{"id", {"command", "line"}, {{"env", "vars"}}},
                          {{"path0", ArtifactDescription{path{"input0"}}},
                           {"path1", ArtifactDescription{path{"input1"}}}}};
    auto const& action = desc.GraphAction();
    auto json = ArtifactFactory::DescribeAction(desc.OutputFiles(),
                                                desc.OutputDirs(),
                                                action.Command(),
                                                desc.Inputs(),
                                                action.Env());

    SECTION("Parse full action") {
        auto description = ActionDescription::FromJson("id", json);
        REQUIRE(description);
        CHECK(description->ToJson() == json);
    }

    SECTION("Parse action without optional input") {
        json["input"] = nlohmann::json::object();
        CHECK(ActionDescription::FromJson("id", json));

        json["input"] = nlohmann::json::array();
        CHECK_FALSE(ActionDescription::FromJson("id", json));

        json.erase("input");
        CHECK(ActionDescription::FromJson("id", json));
    }

    SECTION("Parse action without optional env") {
        json["env"] = nlohmann::json::object();
        CHECK(ActionDescription::FromJson("id", json));

        json["env"] = nlohmann::json::array();
        CHECK_FALSE(ActionDescription::FromJson("id", json));

        json.erase("env");
        CHECK(ActionDescription::FromJson("id", json));
    }

    SECTION("Parse action without mandatory outputs") {
        json["output"] = nlohmann::json::array();
        json["output_dirs"] = nlohmann::json::array();
        CHECK_FALSE(ActionDescription::FromJson("id", json));

        json["output"] = nlohmann::json::object();
        json["output_dirs"] = nlohmann::json::object();
        CHECK_FALSE(ActionDescription::FromJson("id", json));

        json.erase("output");
        json.erase("output_dirs");
        CHECK_FALSE(ActionDescription::FromJson("id", json));
    }

    SECTION("Parse action without mandatory command") {
        json["command"] = nlohmann::json::array();
        CHECK_FALSE(ActionDescription::FromJson("id", json));

        json["command"] = nlohmann::json::object();
        CHECK_FALSE(ActionDescription::FromJson("id", json));

        json.erase("command");
        CHECK_FALSE(ActionDescription::FromJson("id", json));
    }
}
