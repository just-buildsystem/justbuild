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
auto const kTreeId = std::string{"e51a219a27b672ccf17abec7d61eb4d6e0424140"};
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
    CHECK_FALSE(root.IsFile("baz"));
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
    TestFileRootReadEntries(root, "baz", true);
    TestFileRootReadEntries(root, "baz/baz", false);
}

void TestFileRootReadFileType(FileRoot const& root) {
    auto foo_type = root.FileType("baz/foo");
    REQUIRE(foo_type);
    CHECK(*foo_type == ObjectType::File);

    auto bar_type = root.FileType("baz/baz/bar");
    REQUIRE(bar_type);
    CHECK(*bar_type == ObjectType::Executable);

    CHECK_FALSE(root.FileType("baz"));
    CHECK_FALSE(root.FileType("does_not_exist"));
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

        auto bar = root->ToArtifactDescription("baz/baz/bar", "repo");
        REQUIRE(bar);
        CHECK(*bar ==
              ArtifactDescription{ArtifactDigest{kBarId, 3, /*is_tree=*/false},
                                  ObjectType::Executable,
                                  "repo"});

        CHECK_FALSE(root->ToArtifactDescription("baz", "repo"));
        CHECK_FALSE(root->ToArtifactDescription("does_not_exist", "repo"));
    }
}
