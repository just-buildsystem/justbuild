#include <filesystem>
#include <utility>

#include "catch2/catch.hpp"
#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/source_map.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "test/buildtool/build_engine/base_maps/test_repo.hpp"

namespace {

using namespace BuildMaps::Base;  // NOLINT

void SetupConfig(bool use_git) {
    auto root = FileRoot{kBasePath / "data_src"};
    if (use_git) {
        auto repo_path = CreateTestRepo();
        REQUIRE(repo_path);
        auto git_root = FileRoot::FromGit(*repo_path, kSrcTreeId);
        REQUIRE(git_root);
        root = std::move(*git_root);
    }
    RepositoryConfig::Instance().Reset();
    RepositoryConfig::Instance().SetInfo(
        "", RepositoryConfig::RepositoryInfo{root});
}

auto ReadSourceTarget(
    EntityName const& id,
    SourceTargetMap::Consumer consumer,
    bool use_git = false,
    std::optional<SourceTargetMap::FailureFunction> fail_func = std::nullopt)
    -> bool {
    SetupConfig(use_git);
    auto directory_entries = CreateDirectoryEntriesMap();
    auto source_artifacts = CreateSourceTargetMap(&directory_entries);
    std::string error_msg;
    bool success{true};
    {
        TaskSystem ts;
        source_artifacts.ConsumeAfterKeysReady(
            &ts,
            {id},
            std::move(consumer),
            [&success, &error_msg](std::string const& msg, bool /*unused*/) {
                success = false;
                error_msg = msg;
            },
            fail_func ? std::move(*fail_func) : [] {});
    }
    return success and error_msg.empty();
}

}  // namespace

TEST_CASE("from file") {
    nlohmann::json artifacts;
    auto name = EntityName{"", ".", "file"};
    auto consumer = [&artifacts](auto values) {
        artifacts = (*values[0])->Artifacts()->ToJson();
    };

    SECTION("via file") {
        CHECK(ReadSourceTarget(name, consumer, /*use_git=*/false));
        CHECK(artifacts["file"]["type"] == "LOCAL");
        CHECK(artifacts["file"]["data"]["path"] == "file");
    }

    SECTION("via git tree") {
        CHECK(ReadSourceTarget(name, consumer, /*use_git=*/true));
        CHECK(artifacts["file"]["type"] == "KNOWN");
        CHECK(artifacts["file"]["data"]["id"] ==
              "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
        CHECK(artifacts["file"]["data"]["size"] == 0);
    }
}

TEST_CASE("not present at all") {
    bool consumed{false};
    bool failure_called{false};
    auto name = EntityName{"", ".", "does_not_exist"};
    auto consumer = [&consumed](auto /*unused*/) { consumed = true; };
    auto fail_func = [&failure_called]() { failure_called = true; };

    SECTION("via file") {
        CHECK_FALSE(
            ReadSourceTarget(name, consumer, /*use_git=*/false, fail_func));
        CHECK_FALSE(consumed);
        CHECK(failure_called);
    }

    SECTION("via git tree") {
        CHECK_FALSE(
            ReadSourceTarget(name, consumer, /*use_git=*/true, fail_func));
        CHECK_FALSE(consumed);
        CHECK(failure_called);
    }
}

TEST_CASE("malformed entry") {
    bool consumed{false};
    bool failure_called{false};
    auto name = EntityName{"", ".", "bad_entry"};
    auto consumer = [&consumed](auto /*unused*/) { consumed = true; };
    auto fail_func = [&failure_called]() { failure_called = true; };

    SECTION("via git tree") {
        CHECK_FALSE(
            ReadSourceTarget(name, consumer, /*use_git=*/false, fail_func));
        CHECK_FALSE(consumed);
        CHECK(failure_called);
    }

    SECTION("via git tree") {
        CHECK_FALSE(
            ReadSourceTarget(name, consumer, /*use_git=*/true, fail_func));
        CHECK_FALSE(consumed);
        CHECK(failure_called);
    }
}

TEST_CASE("subdir file") {
    nlohmann::json artifacts;
    auto name = EntityName{"", "foo", "bar/file"};
    auto consumer = [&artifacts](auto values) {
        artifacts = (*values[0])->Artifacts()->ToJson();
    };

    SECTION("via file") {
        CHECK(ReadSourceTarget(name, consumer, /*use_git=*/false));
        CHECK(artifacts["bar/file"]["type"] == "LOCAL");
        CHECK(artifacts["bar/file"]["data"]["path"] == "foo/bar/file");
    }

    SECTION("via git tree") {
        CHECK(ReadSourceTarget(name, consumer, /*use_git=*/true));
        CHECK(artifacts["bar/file"]["type"] == "KNOWN");
        CHECK(artifacts["bar/file"]["data"]["id"] ==
              "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
        CHECK(artifacts["bar/file"]["data"]["size"] == 0);
    }
}
