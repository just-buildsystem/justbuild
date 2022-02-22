#include "catch2/catch.hpp"
#include "src/buildtool/common/artifact_factory.hpp"

TEST_CASE("Description missing mandatory key/value pair",
          "[artifact_factory]") {

    nlohmann::json const missing_type = {{"data", {{"path", "some/path"}}}};
    CHECK(not ArtifactFactory::FromDescription(missing_type));
    nlohmann::json const missing_data = {{"type", "LOCAL"}};
    CHECK(not ArtifactFactory::FromDescription(missing_data));
}

TEST_CASE("Local artifact description contains incorrect value for \"data\"",
          "[artifact_factory]") {
    nlohmann::json const local_art_missing_path = {
        {"type", "LOCAL"}, {"data", nlohmann::json::object()}};
    CHECK(not ArtifactFactory::FromDescription(local_art_missing_path));
}

TEST_CASE("Known artifact description contains incorrect value for \"data\"",
          "[artifact_factory]") {
    std::string file_type{};
    file_type += ToChar(ObjectType::File);
    SECTION("missing \"id\"") {
        nlohmann::json const known_art_missing_id = {
            {"type", "KNOWN"},
            {"data", {{"size", 15}, {"file_type", file_type}}}};
        CHECK(not ArtifactFactory::FromDescription(known_art_missing_id));
    }
    SECTION("missing \"size\"") {
        nlohmann::json const known_art_missing_size = {
            {"type", "KNOWN"},
            {"data", {{"id", "known_input"}, {"file_type", file_type}}}};
        CHECK(not ArtifactFactory::FromDescription(known_art_missing_size));
    }
    SECTION("missing \"file_type\"") {
        nlohmann::json const known_art_missing_file_type = {
            {"type", "KNOWN"}, {"data", {{"id", "known_input"}, {"size", 15}}}};

        CHECK(
            not ArtifactFactory::FromDescription(known_art_missing_file_type));
    }
}

TEST_CASE("Action artifact description contains incorrect value for \"data\"",
          "[artifact_factory]") {
    nlohmann::json const action_art_missing_id = {
        {"type", "ACTION"}, {"data", {{"path", "output/path"}}}};
    CHECK(not ArtifactFactory::FromDescription(action_art_missing_id));

    nlohmann::json const action_art_missing_path = {
        {"type", "ACTION"}, {"data", {{"id", "action_id"}}}};
    CHECK(not ArtifactFactory::FromDescription(action_art_missing_path));
}
