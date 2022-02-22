#include "catch2/catch.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/artifact_factory.hpp"

[[nodiscard]] auto operator==(Artifact const& lhs, Artifact const& rhs)
    -> bool {
    return lhs.Id() == rhs.Id() and lhs.FilePath() == rhs.FilePath() and
           lhs.Info() == rhs.Info();
}

TEST_CASE("Local artifact", "[artifact_description]") {
    auto local_desc =
        ArtifactDescription{std::filesystem::path{"local_path"}, "repo"};
    auto local = local_desc.ToArtifact();
    auto local_from_factory =
        ArtifactFactory::FromDescription(local_desc.ToJson());
    CHECK(local == *local_from_factory);
}

TEST_CASE("Known artifact", "[artifact_description]") {
    SECTION("File object") {
        auto known_desc = ArtifactDescription{
            ArtifactDigest{std::string{"f_fake_hash"}, 0}, ObjectType::File};
        auto known = known_desc.ToArtifact();
        auto known_from_factory =
            ArtifactFactory::FromDescription(known_desc.ToJson());
        CHECK(known == *known_from_factory);
    }
    SECTION("Executable object") {
        auto known_desc =
            ArtifactDescription{ArtifactDigest{std::string{"x_fake_hash"}, 1},
                                ObjectType::Executable};
        auto known = known_desc.ToArtifact();
        auto known_from_factory =
            ArtifactFactory::FromDescription(known_desc.ToJson());
        CHECK(known == *known_from_factory);
    }
}

TEST_CASE("Action artifact", "[artifact_description]") {
    auto action_desc =
        ArtifactDescription{"action_id", std::filesystem::path{"out_path"}};
    auto action = action_desc.ToArtifact();
    auto action_from_factory =
        ArtifactFactory::FromDescription(action_desc.ToJson());
    CHECK(action == *action_from_factory);
}

TEST_CASE("From JSON", "[artifact_description]") {
    auto local = ArtifactFactory::DescribeLocalArtifact("local", "repo");
    auto known =
        ArtifactFactory::DescribeKnownArtifact("hash", 0, ObjectType::File);
    auto action = ArtifactFactory::DescribeActionArtifact("id", "output");

    SECTION("Parse artifacts") {
        CHECK(ArtifactDescription::FromJson(local));
        CHECK(ArtifactDescription::FromJson(known));
        CHECK(ArtifactDescription::FromJson(action));
    }

    SECTION("Parse artifact without mandatory type") {
        local.erase("type");
        known.erase("type");
        action.erase("type");
        CHECK_FALSE(ArtifactDescription::FromJson(local));
        CHECK_FALSE(ArtifactDescription::FromJson(known));
        CHECK_FALSE(ArtifactDescription::FromJson(action));
    }

    SECTION("Parse artifact without mandatory data") {
        local.erase("data");
        known.erase("data");
        action.erase("data");
        CHECK_FALSE(ArtifactDescription::FromJson(local));
        CHECK_FALSE(ArtifactDescription::FromJson(known));
        CHECK_FALSE(ArtifactDescription::FromJson(action));
    }

    SECTION("Parse local artifact without mandatory path") {
        local["data"]["path"] = 0;
        CHECK_FALSE(ArtifactDescription::FromJson(local));

        local["data"].erase("path");
        CHECK_FALSE(ArtifactDescription::FromJson(local));
    }

    SECTION("Parse known artifact") {
        SECTION("without mandatory id") {
            known["data"]["id"] = 0;
            CHECK_FALSE(ArtifactDescription::FromJson(known));

            known["data"].erase("id");
            CHECK_FALSE(ArtifactDescription::FromJson(known));
        }
        SECTION("without mandatory size") {
            known["data"]["size"] = "0";
            CHECK_FALSE(ArtifactDescription::FromJson(known));

            known["data"].erase("size");
            CHECK_FALSE(ArtifactDescription::FromJson(known));
        }
        SECTION("without mandatory file_type") {
            known["data"]["file_type"] = "more_than_one_char";
            CHECK_FALSE(ArtifactDescription::FromJson(known));

            known["data"].erase("file_type");
            CHECK_FALSE(ArtifactDescription::FromJson(known));
        }
    }

    SECTION("Parse action artifact") {
        SECTION("without mandatory id") {
            action["data"]["id"] = 0;
            CHECK_FALSE(ArtifactDescription::FromJson(action));

            action["data"].erase("id");
            CHECK_FALSE(ArtifactDescription::FromJson(action));
        }
        SECTION("without mandatory path") {
            action["data"]["path"] = 0;
            CHECK_FALSE(ArtifactDescription::FromJson(action));

            action["data"].erase("path");
            CHECK_FALSE(ArtifactDescription::FromJson(action));
        }
    }
}
