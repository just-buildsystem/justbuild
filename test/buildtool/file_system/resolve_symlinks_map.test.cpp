// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/file_system/symlinks_map/resolve_symlinks_map.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "fmt/core.h"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "test/utils/shell_quoting.hpp"

namespace {

// see create_fs_test_git_bundle_symlinks.sh
auto const kBundleSymPath =
    std::string{"test/buildtool/file_system/data/test_repo_symlinks.bundle"};
auto const kTreeSymId = std::string{"18770dacfe14c15d88450c21c16668e13ab0e7f9"};
auto const kFooId = std::string{"19102815663d23f8b75a47e7a01965dcdc96468c"};
auto const kBarId = std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};
auto const kBazLinkId = std::string{"3f9538666251333f5fa519e01eb267d371ca9c78"};
auto const kBazSymId = std::string{"1868f82682c290f0b1db3cacd092727eef1fa57f"};
auto const kBazBarLinkId =
    std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};
auto const kFooLinkId = std::string{"b24736f10d3c60015386047ebc98b4ab63056041"};

// see create_fs_test_git_bundle.sh
auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo.bundle"};
auto const kBazId = std::string{"27b32561185c2825150893774953906c6daa6798"};

[[nodiscard]] auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/file_system";
}

[[nodiscard]] auto CreateTestRepo(bool is_bare = false)
    -> std::optional<std::filesystem::path> {
    static std::atomic<int> counter{};
    auto repo_path =
        GetTestDir() / "test_repo" /
        std::filesystem::path{std::to_string(counter++)}.filename();
    auto cmd = fmt::format("git clone {}{} {}",
                           is_bare ? "--bare " : "",
                           QuoteForShell(kBundlePath),
                           QuoteForShell(repo_path.string()));
    if (std::system(cmd.c_str()) == 0) {
        return repo_path;
    }
    return std::nullopt;
}

[[nodiscard]] auto CreateTestRepoSymlinks(bool is_bare = false)
    -> std::optional<std::filesystem::path> {
    static std::atomic<int> counter{};
    auto repo_path =
        GetTestDir() / "test_repo_symlinks" /
        std::filesystem::path{std::to_string(counter++)}.filename();
    auto cmd = fmt::format("git clone {}{} {}",
                           is_bare ? "--bare " : "",
                           QuoteForShell(kBundleSymPath),
                           QuoteForShell(repo_path.string()));
    if (std::system(cmd.c_str()) == 0) {
        return repo_path;
    }
    return std::nullopt;
}

// The checkout will make the content available, as well as the HEAD ref
[[nodiscard]] auto CreateTestRepoSymlinksWithCheckout(
    bool is_bare = false) noexcept -> std::optional<std::filesystem::path> {
    auto repo_path = CreateTestRepoSymlinks(is_bare);
    REQUIRE(repo_path);
    auto cmd =
        fmt::format("git --git-dir={} --work-tree={} checkout master",
                    QuoteForShell(is_bare ? repo_path->string()
                                          : (*repo_path / ".git").string()),
                    QuoteForShell(repo_path->string()));
    if (std::system(cmd.c_str()) == 0) {
        return repo_path;
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("Resolve symlinks", "[resolve_symlinks_map]") {
    // non-bare repo with symlinks as source
    auto source_repo_path = CreateTestRepoSymlinksWithCheckout();
    REQUIRE(source_repo_path);
    auto source_cas = GitCAS::Open(*source_repo_path);
    REQUIRE(source_cas);

    auto resolve_symlinks_map = CreateResolveSymlinksMap();

    SECTION("Source repo is target repo") {
        constexpr auto NUM_CASES = 3;
        std::vector<ResolvedGitObject> expected = {
            {kFooId, ObjectType::File, "baz/foo"},
            {kBazBarLinkId, ObjectType::Symlink, "bar_l"},
            {kBazId, ObjectType::Tree, "."}};

        auto error = false;
        auto error_msg = std::string("NONE");
        {
            TaskSystem ts;
            resolve_symlinks_map.ConsumeAfterKeysReady(
                &ts,
                {GitObjectToResolve(kTreeSymId,
                                    "foo_l",
                                    PragmaSpecial::ResolveCompletely,
                                    /*known_info=*/std::nullopt,
                                    source_cas,
                                    source_cas),
                 GitObjectToResolve(kBazSymId,
                                    "bar_l",
                                    PragmaSpecial::ResolvePartially,
                                    /*known_info=*/std::nullopt,
                                    source_cas,
                                    source_cas),
                 GitObjectToResolve(kBazSymId,
                                    ".",
                                    PragmaSpecial::Ignore,
                                    /*known_info=*/std::nullopt,
                                    source_cas,
                                    source_cas)},
                [&expected, &source_cas](auto const& values) {
                    for (auto i = 0; i < NUM_CASES; ++i) {
                        auto const& res = ResolvedGitObject{*values[i]};
                        CHECK(res.id == expected[i].id);
                        CHECK(res.type == expected[i].type);
                        CHECK(res.path == expected[i].path);
                        // the object needs to be present in target repo
                        CHECK(
                            source_cas->ReadHeader(res.id, /*is_hex_id=*/true));
                    }
                },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK_FALSE(error);
        CHECK(error_msg == "NONE");
    }

    SECTION("Bare target repo") {
        auto target_repo_path = CreateTestRepo(true);
        REQUIRE(target_repo_path);
        auto target_cas = GitCAS::Open(*target_repo_path);
        REQUIRE(target_cas);

        constexpr auto NUM_CASES = 3;
        std::vector<ResolvedGitObject> expected = {
            {kFooId, ObjectType::File, "baz/foo"},
            {kBazBarLinkId, ObjectType::Symlink, "bar_l"},
            {kBazId, ObjectType::Tree, "."}};

        auto error = false;
        auto error_msg = std::string("NONE");
        {
            TaskSystem ts;
            resolve_symlinks_map.ConsumeAfterKeysReady(
                &ts,
                {GitObjectToResolve(kTreeSymId,
                                    "foo_l",
                                    PragmaSpecial::ResolveCompletely,
                                    /*known_info=*/std::nullopt,
                                    source_cas,
                                    target_cas),
                 GitObjectToResolve(kBazSymId,
                                    "bar_l",
                                    PragmaSpecial::ResolvePartially,
                                    /*known_info=*/std::nullopt,
                                    source_cas,
                                    target_cas),
                 GitObjectToResolve(kBazSymId,
                                    ".",
                                    PragmaSpecial::Ignore,
                                    /*known_info=*/std::nullopt,
                                    source_cas,
                                    target_cas)},
                [&expected, &target_cas](auto const& values) {
                    for (auto i = 0; i < NUM_CASES; ++i) {
                        auto const& res = ResolvedGitObject{*values[i]};
                        CHECK(res.id == expected[i].id);
                        CHECK(res.type == expected[i].type);
                        CHECK(res.path == expected[i].path);
                        // the object needs to be present in target repo
                        CHECK(
                            target_cas->ReadHeader(res.id, /*is_hex_id=*/true));
                    }
                },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK_FALSE(error);
        CHECK(error_msg == "NONE");
    }
}
