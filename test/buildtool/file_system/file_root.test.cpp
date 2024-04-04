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
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "test/utils/container_matchers.hpp"
#include "test/utils/shell_quoting.hpp"

namespace {

auto const kBundleSymPath =
    std::string{"test/buildtool/file_system/data/test_repo_symlinks.bundle"};
auto const kTreeSymId = std::string{"18770dacfe14c15d88450c21c16668e13ab0e7f9"};
auto const kFooId = std::string{"19102815663d23f8b75a47e7a01965dcdc96468c"};
auto const kBarId = std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};

[[nodiscard]] auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/file_system";
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

void TestFileRootReadCommonFiles(FileRoot const& root) {
    REQUIRE(root.Exists("foo"));
    REQUIRE(root.IsFile("foo"));
    auto foo = root.ReadContent("foo");
    REQUIRE(foo);
    CHECK(*foo == "foo");

    REQUIRE(root.Exists("bar"));
    REQUIRE(root.IsFile("bar"));
    auto bar = root.ReadContent("bar");
    REQUIRE(bar);
    CHECK(*bar == "bar");

    REQUIRE(root.Exists("baz"));
    REQUIRE(root.IsDirectory("baz"));

    // Check subdir
    REQUIRE(root.Exists("baz/foo"));
    REQUIRE(root.IsFile("baz/foo"));
    auto bazfoo = root.ReadContent("baz/foo");
    REQUIRE(bazfoo);
    CHECK(*bazfoo == "foo");

    REQUIRE(root.Exists("baz/bar"));
    REQUIRE(root.IsFile("baz/bar"));
    auto bazbar = root.ReadContent("baz/bar");
    REQUIRE(bazbar);
    CHECK(*bazbar == "bar");
}

void TestFileRootReadFilesOnly(FileRoot const& root) {
    // Check common files
    TestFileRootReadCommonFiles(root);

    // Check symlinks are missing
    CHECK_FALSE(root.Exists("baz_l"));
    CHECK_FALSE(root.Exists("foo_l"));
    CHECK_FALSE(root.Exists("baz/bar_l"));
}

void TestFileRootReadFilesAndSymlinks(FileRoot const& root) {
    // Check common files
    TestFileRootReadCommonFiles(root);

    // Check symlinks
    CHECK(root.Exists("baz_l"));
    CHECK(root.Exists("foo_l"));
    CHECK(root.Exists("baz/bar_l"));
}

void TestFileRootReadEntries(FileRoot const& root,
                             std::string const& path,
                             bool has_baz,
                             bool with_symlinks) {
    REQUIRE(root.Exists(path));
    REQUIRE(root.IsDirectory(path));
    auto entries = root.ReadDirectory(path);

    CHECK_FALSE(entries.Empty());
    CHECK(entries.ContainsBlob("foo"));
    CHECK(entries.ContainsBlob("bar"));
    if (has_baz) {
        CHECK_FALSE(entries.ContainsBlob("baz"));
        CHECK(with_symlinks == entries.ContainsBlob("baz_l"));
        CHECK(with_symlinks == entries.ContainsBlob("foo_l"));
    }
    else {
        CHECK(with_symlinks == entries.ContainsBlob("bar_l"));
    }
    CHECK_FALSE(entries.ContainsBlob("does_not_exist"));
}

void TestFileRootReadDirectory(FileRoot const& root, bool with_symlinks) {
    TestFileRootReadEntries(root, ".", true, with_symlinks);
    TestFileRootReadEntries(root, "baz", false, with_symlinks);
}

void TestFileRootReadBlobType(FileRoot const& root) {
    auto foo_type = root.BlobType("baz/foo");
    REQUIRE(foo_type);
    CHECK(*foo_type == ObjectType::File);

    auto bar_type = root.BlobType("baz/bar");
    REQUIRE(bar_type);
    CHECK(*bar_type == ObjectType::Executable);

    CHECK_FALSE(root.BlobType("baz"));
    CHECK_FALSE(root.BlobType("does_not_exist"));

    // Check subdir
    REQUIRE(root.Exists("baz/foo"));
    REQUIRE(root.IsFile("baz/foo"));
    auto bazfoo = root.ReadContent("baz/foo");
    REQUIRE(bazfoo);
    CHECK(*bazfoo == "foo");

    REQUIRE(root.Exists("baz/bar"));
    REQUIRE(root.IsFile("baz/bar"));
    auto bazbar = root.ReadContent("baz/bar");
    REQUIRE(bazbar);
    CHECK(*bazbar == "bar");
}

}  // namespace

TEST_CASE("Creating file root", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        CHECK(FileRoot{*root_path}.Exists("."));
        CHECK_FALSE(
            FileRoot{std::filesystem::path{"does_not_exist"}}.Exists("."));
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);

        auto root = FileRoot::FromGit(*repo_path, kTreeSymId);
        REQUIRE(root);
        CHECK(root->Exists("."));

        CHECK_FALSE(FileRoot::FromGit("does_not_exist", kTreeSymId));
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        CHECK(FileRoot{*root_path, /*ignore_special=*/true}.Exists("."));
        CHECK_FALSE(FileRoot{std::filesystem::path{"does_not_exist"},
                             /*ignore_special=*/true}
                        .Exists("."));
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
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadFilesAndSymlinks(FileRoot{*root_path});
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeSymId);
        REQUIRE(root);

        TestFileRootReadFilesAndSymlinks(*root);
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadFilesOnly(
            FileRoot{*root_path, /*ignore_special=*/true});
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);

        TestFileRootReadFilesOnly(*root);
    }
}

TEST_CASE("Reading directories", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadDirectory(FileRoot{*root_path}, /*with_symlinks=*/true);
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeSymId);
        REQUIRE(root);

        TestFileRootReadDirectory(*root, /*with_symlinks=*/true);
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadDirectory(FileRoot{*root_path, /*ignore_special=*/true},
                                  /*with_symlinks=*/false);
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);

        TestFileRootReadDirectory(*root, /*with_symlinks=*/false);
    }
}

TEST_CASE("Reading blobs", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        CHECK_FALSE(FileRoot{*root_path}.ReadBlob(kFooId));
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeSymId);
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

TEST_CASE("Reading blob type", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadBlobType(FileRoot{*root_path});
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeSymId);
        REQUIRE(root);

        TestFileRootReadBlobType(*root);
    }

    SECTION("local root ignore-special") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);

        TestFileRootReadBlobType(FileRoot{*root_path, /*ignore_special=*/true});
    }

    SECTION("git root ignore-special") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root =
            FileRoot::FromGit(*repo_path, kTreeSymId, /*ignore_special=*/true);
        REQUIRE(root);

        TestFileRootReadBlobType(*root);
    }
}

TEST_CASE("Creating artifact descriptions", "[file_root]") {
    SECTION("local root") {
        auto root_path = CreateTestRepoSymlinks(true);
        REQUIRE(root_path);
        auto root = FileRoot{*root_path};

        auto desc = root.ToArtifactDescription("baz/foo", "repo");
        REQUIRE(desc);
        CHECK(*desc ==
              ArtifactDescription(std::filesystem::path{"baz/foo"}, "repo"));

        CHECK(root.ToArtifactDescription("does_not_exist", "repo"));
    }

    SECTION("git root") {
        auto repo_path = CreateTestRepoSymlinks(false);
        REQUIRE(repo_path);
        auto root = FileRoot::FromGit(*repo_path, kTreeSymId);
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
