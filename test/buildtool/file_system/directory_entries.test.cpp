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

}  // namespace

TEST_CASE("Get entries of a directory", "[directory_entries]") {
    const std::unordered_set<std::string> reference_entries{
        "test_repo.bundle", "empty_executable", "subdir", "example_file"};

    const auto dir = std::filesystem::path("test/buildtool/file_system/data");

    auto fs_root = FileRoot();
    auto dir_entries = fs_root.ReadDirectory(dir);
    CHECK(dir_entries.ContainsFile("test_repo.bundle"));
    CHECK(dir_entries.ContainsFile("empty_executable"));
    CHECK(dir_entries.ContainsFile("example_file"));
    {
        // all the entries returned by FilesIterator are files,
        // are contained in reference_entries,
        // and are 3
        auto counter = 0;
        for (const auto& x : dir_entries.FilesIterator()) {
            REQUIRE(reference_entries.contains(x));
            CHECK(dir_entries.ContainsFile(x));
            ++counter;
        }

        CHECK(counter == 3);
    }
    {
        // all the entries returned by DirectoriesIterator are not files (e.g.,
        // trees),
        // are contained in reference_entries,
        // and are 1

        auto counter = 0;
        for (const auto& x : dir_entries.DirectoriesIterator()) {
            REQUIRE(reference_entries.contains(x));
            CHECK_FALSE(dir_entries.ContainsFile(x));
            ++counter;
        }

        CHECK(counter == 1);
    }
}

TEST_CASE("Get entries of a git tree", "[directory_entries]") {
    auto reference_entries =
        std::unordered_set<std::string>{"foo", "bar", "baz", ".git"};
    auto repo = *CreateTestRepo(true);
    auto fs_root = FileRoot();
    auto dir_entries = fs_root.ReadDirectory(repo);
    CHECK(dir_entries.ContainsFile("bar"));
    CHECK(dir_entries.ContainsFile("foo"));
    CHECK_FALSE(dir_entries.ContainsFile("baz"));
    {
        // all the entries returned by FilesIterator are files,
        // are contained in reference_entries,
        // and are 2 (foo, and bar)
        auto counter = 0;
        for (const auto& x : dir_entries.FilesIterator()) {
            REQUIRE(reference_entries.contains(x));
            CHECK(dir_entries.ContainsFile(x));
            ++counter;
        }

        CHECK(counter == 2);
    }
    {
        // all the entries returned by DirectoriesIterator are not files (e.g.,
        // trees),
        // are contained in reference_entries,
        // and are 2 (baz, and .git)
        auto counter = 0;
        for (const auto& x : dir_entries.DirectoriesIterator()) {
            REQUIRE(reference_entries.contains(x));
            CHECK_FALSE(dir_entries.ContainsFile(x));
            ++counter;
        }

        CHECK(counter == 2);
    }
}

TEST_CASE("Get entries of an empty directory", "[directory_entries]") {
    // CreateDirectoryEntriesMap returns
    // FileRoot::DirectoryEntries{FileRoot::DirectoryEntries::pairs_t{}} in case
    // of a missing directory, which represents an empty directory

    auto dir_entries =
        FileRoot::DirectoryEntries{FileRoot::DirectoryEntries::pairs_t{}};
    // of course, no files should be there
    CHECK_FALSE(dir_entries.ContainsFile("test_repo.bundle"));
    {
        // FilesIterator should be an empty range
        auto counter = 0;
        for (const auto& x : dir_entries.FilesIterator()) {
            CHECK_FALSE(dir_entries.ContainsFile(x));  // should never be called
            ++counter;
        }

        CHECK(counter == 0);
    }
    {
        // DirectoriesIterator should be an empty range
        auto counter = 0;
        for (const auto& x : dir_entries.DirectoriesIterator()) {
            CHECK(dir_entries.ContainsFile(x));  // should never be called
            ++counter;
        }

        CHECK(counter == 0);
    }
}
