#include <filesystem>

#include "catch2/catch.hpp"
#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/common/repository_config.hpp"
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

auto ReadDirectory(ModuleName const& id,
                   DirectoryEntriesMap::Consumer value_checker,
                   bool use_git = false) -> bool {
    SetupConfig(use_git);
    auto data_direntries = CreateDirectoryEntriesMap();
    bool success{true};
    {
        TaskSystem ts;
        data_direntries.ConsumeAfterKeysReady(
            &ts,
            {id},
            std::move(value_checker),
            [&success](std::string const& /*unused*/, bool /*unused*/) {
                success = false;
            });
    }
    return success;
}

}  // namespace

TEST_CASE("simple usage") {
    bool as_expected{false};
    auto name = ModuleName{"", "."};
    auto consumer = [&as_expected](auto values) {
        if (values[0]->ContainsFile("file") &&
            not values[0]->ContainsFile("does_not_exist")) {
            as_expected = true;
        };
    };

    SECTION("via file") {
        CHECK(ReadDirectory(name, consumer, /*use_git=*/false));
        CHECK(as_expected);
    }

    SECTION("via git tree") {
        CHECK(ReadDirectory(name, consumer, /*use_git=*/true));
        CHECK(as_expected);
    }
}

TEST_CASE("missing directory") {
    bool as_expected{false};
    auto name = ModuleName{"", "does_not_exist"};
    auto consumer = [&as_expected](auto values) {
        if (values[0]->Empty()) {
            as_expected = true;
        }
    };

    SECTION("via file") {
        CHECK(ReadDirectory(name, consumer, /*use_git=*/false));
        CHECK(as_expected);
    }

    SECTION("via git tree") {
        CHECK(ReadDirectory(name, consumer, /*use_git=*/true));
        CHECK(as_expected);
    }
}
