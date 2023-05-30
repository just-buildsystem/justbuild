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

#include <atomic>
#include <thread>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "test/utils/container_matchers.hpp"
#include "test/utils/shell_quoting.hpp"

namespace {

auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo.bundle"};
auto const kTreeId = std::string{"c610db170fbcad5f2d66fe19972495923f3b2536"};
auto const kFooId = std::string{"19102815663d23f8b75a47e7a01965dcdc96468c"};
auto const kBarId = std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};

auto const kBundleSymPath =
    std::string{"test/buildtool/file_system/data/test_repo_symlinks.bundle"};
auto const kTreeSymId = std::string{"18770dacfe14c15d88450c21c16668e13ab0e7f9"};

[[nodiscard]] auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/file_system";
}

[[nodiscard]] auto CreateTestRepo(bool do_checkout = false)
    -> std::optional<std::filesystem::path> {
    static std::atomic<int> counter{};
    auto repo_path =
        GetTestDir() / "test_repo" /
        std::filesystem::path{std::to_string(counter++)}.filename();
    auto cmd = fmt::format("git clone {}{} {}",
                           do_checkout ? "--branch master " : "",
                           QuoteForShell(kBundlePath),
                           QuoteForShell(repo_path.string()));
    if (std::system(cmd.c_str()) == 0) {
        return repo_path;
    }
    return std::nullopt;
}

[[nodiscard]] auto CreateTestRepoSymlinks(bool do_checkout = false)
    -> std::optional<std::filesystem::path> {
    static std::atomic<int> counter{};
    auto repo_path =
        GetTestDir() / "test_repo_symlinks" /
        std::filesystem::path{std::to_string(counter++)}.filename();
    auto cmd = fmt::format("git clone {}{} {}",
                           do_checkout ? "--branch master " : "",
                           QuoteForShell(kBundleSymPath),
                           QuoteForShell(repo_path.string()));
    if (std::system(cmd.c_str()) == 0) {
        return repo_path;
    }
    return std::nullopt;
}

void TestFileRootReadFile(FileRoot const& root) {
    REQUIRE(root.Exists("foo"));
    REQUIRE(root.IsFile("foo"));
    auto foo = root.ReadFile("foo");
    REQUIRE(foo);
    CHECK(*foo == "foo");

    REQUIRE(root.Exists("bar"));
    REQUIRE(root.IsFile("bar"));
    auto bar = root.ReadFile("bar");
    REQUIRE(bar);
    CHECK(*bar == "bar");

    REQUIRE(root.Exists("baz"));
    REQUIRE(root.IsDirectory("baz"));

    // Check subdir
    REQUIRE(root.Exists("baz/foo"));
    REQUIRE(root.IsFile("baz/foo"));
    auto bazfoo = root.ReadFile("baz/foo");
    REQUIRE(bazfoo);
    CHECK(*bazfoo == "foo");

    REQUIRE(root.Exists("baz/bar"));
    REQUIRE(root.IsFile("baz/bar"));
    auto bazbar = root.ReadFile("baz/bar");
    REQUIRE(bazbar);
    CHECK(*bazbar == "bar");

    // Check symlinks are missing
    CHECK_FALSE(root.Exists("baz_l"));
    CHECK_FALSE(root.Exists("foo_l"));
    CHECK_FALSE(root.Exists("baz/bar_l"));
}

void TestFileRootReadEntries(FileRoot const& root,
                             std::string const& path,
                             bool has_baz) {
    REQUIRE(root.Exists(path));
    REQUIRE(root.IsDirectory(path));
    auto entries = root.ReadDirectory(path);

    CHECK_FALSE(entries.Empty());
    CHECK(entries.ContainsFile("foo"));
    CHECK(entries.ContainsFile("bar"));
    if (has_baz) {
        CHECK_FALSE(entries.ContainsFile("baz"));
    }
    CHECK_FALSE(entries.ContainsFile("does_not_exist"));
}

void TestFileRootReadDirectory(FileRoot const& root) {
    TestFileRootReadEntries(root, ".", true);
    TestFileRootReadEntries(root, "baz", false);
}

void TestFileRootReadFileType(FileRoot const& root) {
    auto foo_type = root.FileType("baz/foo");
    REQUIRE(foo_type);
    CHECK(*foo_type == ObjectType::File);

    auto bar_type = root.FileType("baz/bar");
    REQUIRE(bar_type);
    CHECK(*bar_type == ObjectType::Executable);

    CHECK_FALSE(root.FileType("baz"));
    CHECK_FALSE(root.FileType("does_not_exist"));

    // Check subdir
    REQUIRE(root.Exists("baz/foo"));
    REQUIRE(root.IsFile("baz/foo"));
    auto bazfoo = root.ReadFile("baz/foo");
    REQUIRE(bazfoo);
    CHECK(*bazfoo == "foo");

    REQUIRE(root.Exists("baz/bar"));
    REQUIRE(root.IsFile("baz/bar"));
    auto bazbar = root.ReadFile("baz/bar");
    REQUIRE(bazbar);
    CHECK(*bazbar == "bar");
}

}  // namespace

TEST_CASE("Creating file root", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepo(true);
        REQUIRE(root_path);

        CHECK(FileRoot{*root_path}.Exists("."));
        CHECK_FALSE(FileRoot{"does_not_exist"}.Exists("."));
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepo(false);
        REQUIRE(repo_path);

        auto root = FileRoot::FromGit(*repo_path, kTreeId);
        REQUIRE(root);
        CHECK(root->Exists("."));

        CHECK_FALSE(FileRoot::FromGit("does_not_exist", kTreeId));
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        CHECK(FileRoot{*root_path, /*ignore_special=*/true}.Exists("."));
        CHECK_FALSE(
            FileRoot{"does_not_exist", /*ignore_special=*/true}.Exists("."));
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);

        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);
        CHECK(root->Exists("."));

        CHECK_FALSE(FileRoot::FromGit(
            "does_not_exist", kTreeSymId, /*ignore_special=*/true));
    }
}

TEST_CASE("Reading files", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepo(true);
        REQUIRE(root_path);

        TestFileRootReadFile(FileRoot{*root_path});
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepo(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeId);
        REQUIRE(root);

        TestFileRootReadFile(*root);
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadFile(FileRoot{*root_path, /*ignore_special=*/true});
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);

        TestFileRootReadFile(*root);
    }
}

TEST_CASE("Reading directories", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepo(true);
        REQUIRE(root_path);

        TestFileRootReadDirectory(FileRoot{*root_path});
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepo(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeId);
        REQUIRE(root);

        TestFileRootReadDirectory(*root);
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadDirectory(
            FileRoot{*root_path, /*ignore_special=*/true});
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);

        TestFileRootReadDirectory(*root);
    }
}

TEST_CASE("Reading blobs", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepo(true);
        REQUIRE(root_path);

        CHECK_FALSE(FileRoot{*root_path}.ReadBlob(kFooId));
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepo(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeId);
        REQUIRE(root);

        auto foo = root->ReadBlob(kFooId);
        REQUIRE(foo);
        CHECK(*foo == "foo");

        CHECK_FALSE(root->ReadBlob("does_not_exist"));
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        CHECK_FALSE(
            FileRoot{*root_path, /*ignore_special=*/true}.ReadBlob(kFooId));
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);

        auto foo = root->ReadBlob(kFooId);
        REQUIRE(foo);
        CHECK(*foo == "foo");

        CHECK_FALSE(root->ReadBlob("does_not_exist"));
    }
}

TEST_CASE("Reading file type", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepo(true);
        REQUIRE(root_path);

        TestFileRootReadFileType(FileRoot{*root_path});
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepo(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeId);
        REQUIRE(root);

        TestFileRootReadFileType(*root);
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadFileType(FileRoot{*root_path, /*ignore_special=*/true});
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);

        TestFileRootReadFileType(*root);
    }
}

TEST_CASE("Creating artifact descriptions", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepo(true);
        REQUIRE(root_path);
        auto root = FileRoot{*root_path};

        auto desc = root.ToArtifactDescription("baz/foo", "repo");
        REQUIRE(desc);
        CHECK(*desc ==
              ArtifactDescription(std::filesystem::path{"baz/foo"}, "repo"));

        CHECK(root.ToArtifactDescription("does_not_exist", "repo"));
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepo(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeId);
        REQUIRE(root);

        auto foo = root->ToArtifactDescription("baz/foo", "repo");
        REQUIRE(foo);
        CHECK(*foo ==
              ArtifactDescription{ArtifactDigest{kFooId, 3, /*is_tree=*/false},
                                  ObjectType::File,
                                  "repo"});

        auto bar = root->ToArtifactDescription("baz/bar", "repo");
        REQUIRE(bar);
        CHECK(*bar ==
              ArtifactDescription{ArtifactDigest{kBarId, 3, /*is_tree=*/false},
                                  ObjectType::Executable,
                                  "repo"});

        CHECK_FALSE(root->ToArtifactDescription("baz", "repo"));
        CHECK_FALSE(root->ToArtifactDescription("does_not_exist", "repo"));
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);
        auto root = FileRoot{*root_path, /*ignore_special=*/true};

        auto desc = root.ToArtifactDescription("baz/foo", "repo");
        REQUIRE(desc);
        CHECK(*desc ==
              ArtifactDescription(std::filesystem::path{"baz/foo"}, "repo"));

        CHECK(root.ToArtifactDescription("does_not_exist", "repo"));
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);

        auto foo = root->ToArtifactDescription("baz/foo", "repo");
        REQUIRE(foo);
        CHECK(*foo ==
              ArtifactDescription{ArtifactDigest{kFooId, 3, /*is_tree=*/false},
                                  ObjectType::File,
                                  "repo"});

        auto bar = root->ToArtifactDescription("baz/bar", "repo");
        REQUIRE(bar);
        CHECK(*bar ==
              ArtifactDescription{ArtifactDigest{kBarId, 3, /*is_tree=*/false},
                                  ObjectType::Executable,
                                  "repo"});

        CHECK_FALSE(root->ToArtifactDescription("baz", "repo"));
        CHECK_FALSE(root->ToArtifactDescription("does_not_exist", "repo"));
    }
}
