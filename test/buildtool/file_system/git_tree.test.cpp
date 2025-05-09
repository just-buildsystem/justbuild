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

#include "src/buildtool/file_system/git_tree.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_all.hpp"
#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/atomic.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "test/utils/container_matchers.hpp"
#include "test/utils/shell_quoting.hpp"

namespace {

auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo.bundle"};
auto const kTreeId = std::string{"c610db170fbcad5f2d66fe19972495923f3b2536"};
auto const kFooId = std::string{"19102815663d23f8b75a47e7a01965dcdc96468c"};
auto const kBarId = std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};
auto const kFailId = std::string{"0123456789abcdef0123456789abcdef01234567"};

auto const kBundleSymPath =
    std::string{"test/buildtool/file_system/data/test_repo_symlinks.bundle"};
auto const kTreeSymId = std::string{"18770dacfe14c15d88450c21c16668e13ab0e7f9"};
auto const kBazLinkId = std::string{"3f9538666251333f5fa519e01eb267d371ca9c78"};
auto const kBazBarLinkId =
    std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};
auto const kFooLinkId = std::string{"b24736f10d3c60015386047ebc98b4ab63056041"};

[[nodiscard]] auto HexToRaw(std::string const& hex) -> std::string {
    return FromHexString(hex).value_or<std::string>({});
}
[[nodiscard]] auto RawToHex(std::string const& raw) -> std::string {
    return ToHexString(raw);
}

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

class SymlinksChecker final {
  public:
    explicit SymlinksChecker(gsl::not_null<GitCASPtr> const& cas) noexcept
        : cas_{*cas} {}

    [[nodiscard]] auto operator()(
        std::vector<ArtifactDigest> const& ids) const noexcept -> bool {
        return std::all_of(
            ids.begin(), ids.end(), [&cas = cas_](ArtifactDigest const& id) {
                return cas
                    .ReadObject(id.hash(),
                                /*is_hex_id=*/true,
                                /*as_valid_symlink=*/true)
                    .has_value();
            });
    };

  private:
    GitCAS const& cas_;
};

}  // namespace

TEST_CASE("Open Git CAS", "[git_cas]") {
    SECTION("Bare repository") {
        auto repo_path = CreateTestRepo(true);
        REQUIRE(repo_path);
        CHECK(GitCAS::Open(*repo_path));
    }

    SECTION("Non-bare repository") {
        auto repo_path = CreateTestRepo(false);
        REQUIRE(repo_path);
        CHECK(GitCAS::Open(*repo_path));
    }

    SECTION("Non-existing repository") {
        CHECK_FALSE(GitCAS::Open("does_not_exist"));
    }
}

TEST_CASE("Read Git Objects", "[git_cas]") {
    auto repo_path = CreateTestRepoSymlinks(true);
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);

    SECTION("valid ids") {
        CHECK(cas->ReadObject(kFooId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kFooId), /*is_hex_id=*/false));

        CHECK(cas->ReadObject(kBarId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kBarId), /*is_hex_id=*/false));

        CHECK(cas->ReadObject(kTreeSymId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kTreeSymId), /*is_hex_id=*/false));

        CHECK(cas->ReadObject(kBazBarLinkId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kBazBarLinkId), /*is_hex_id=*/false));

        CHECK(cas->ReadObject(kBazBarLinkId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kBazBarLinkId), /*is_hex_id=*/false));

        CHECK(cas->ReadObject(kFooLinkId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kFooLinkId), /*is_hex_id=*/false));
    }

    SECTION("invalid ids") {
        CHECK_FALSE(cas->ReadObject("", /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadObject("", /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadObject(kFailId, /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadObject(HexToRaw(kFailId), /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadObject(RawToHex("to_short"), /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadObject("to_short", /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadObject("invalid_chars", /*is_hex_id=*/true));
    }
}

TEST_CASE("Read Git Headers", "[git_cas]") {
    auto repo_path = CreateTestRepoSymlinks(true);
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);

    SECTION("valid ids") {
        CHECK(cas->ReadHeader(kFooId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kFooId), /*is_hex_id=*/false));

        CHECK(cas->ReadHeader(kBarId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kBarId), /*is_hex_id=*/false));

        CHECK(cas->ReadHeader(kTreeSymId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kTreeSymId), /*is_hex_id=*/false));

        CHECK(cas->ReadHeader(kBazBarLinkId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kBazBarLinkId), /*is_hex_id=*/false));

        CHECK(cas->ReadHeader(kBazBarLinkId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kBazBarLinkId), /*is_hex_id=*/false));

        CHECK(cas->ReadHeader(kFooLinkId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kFooLinkId), /*is_hex_id=*/false));
    }

    SECTION("invalid ids") {
        CHECK_FALSE(cas->ReadHeader("", /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadHeader("", /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadHeader(kFailId, /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadHeader(HexToRaw(kFailId), /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadHeader(RawToHex("to_short"), /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadHeader("to_short", /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadHeader("invalid_chars", /*is_hex_id=*/true));
    }
}

TEST_CASE("Read Git Trees", "[git_cas]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);

    // create symlinks checker
    auto const check_symlinks = SymlinksChecker{cas};

    SECTION("invalid trees") {
        CHECK_FALSE(repo->ReadTree("", check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(repo->ReadTree("", check_symlinks, /*is_hex_id=*/false));

        CHECK_FALSE(
            repo->ReadTree(kFailId, check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(repo->ReadTree(
            HexToRaw(kFailId), check_symlinks, /*is_hex_id=*/false));

        CHECK_FALSE(repo->ReadTree(
            RawToHex("to_short"), check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(
            repo->ReadTree("to_short", check_symlinks, /*is_hex_id=*/false));

        CHECK_FALSE(repo->ReadTree(
            "invalid_chars", check_symlinks, /*is_hex_id=*/true));

        CHECK_FALSE(repo->ReadTree(kFooId, check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(repo->ReadTree(
            HexToRaw(kFooId), check_symlinks, /*is_hex_id=*/false));

        CHECK_FALSE(repo->ReadTree(kBarId, check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(repo->ReadTree(
            HexToRaw(kBarId), check_symlinks, /*is_hex_id=*/false));
    }

    SECTION("valid trees") {
        auto entries0 =
            repo->ReadTree(kTreeId, check_symlinks, /*is_hex_id=*/true);
        auto entries1 = repo->ReadTree(
            HexToRaw(kTreeId), check_symlinks, /*is_hex_id=*/false);

        REQUIRE(entries0);
        REQUIRE(entries1);
        CHECK(*entries0 == *entries1);
    }
}

TEST_CASE("Read Git Trees with symlinks -- ignore special", "[git_cas]") {
    auto repo_path = CreateTestRepoSymlinks(false);  // checkout needed
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);

    // create symlinks checker
    auto const check_symlinks = SymlinksChecker{cas};

    SECTION("invalid trees") {
        CHECK_FALSE(repo->ReadTree(
            "", check_symlinks, /*is_hex_id=*/true, /*ignore_special=*/true));
        CHECK_FALSE(repo->ReadTree(
            "", check_symlinks, /*is_hex_id=*/false, /*ignore_special=*/true));

        CHECK_FALSE(repo->ReadTree(kFailId,
                                   check_symlinks,
                                   /*is_hex_id=*/true,
                                   /*ignore_special=*/true));
        CHECK_FALSE(repo->ReadTree(HexToRaw(kFailId),
                                   check_symlinks,
                                   /*is_hex_id=*/false,
                                   /*ignore_special=*/true));

        CHECK_FALSE(repo->ReadTree(RawToHex("to_short"),
                                   check_symlinks,
                                   /*is_hex_id=*/true,
                                   /*ignore_special=*/true));
        CHECK_FALSE(repo->ReadTree("to_short",
                                   check_symlinks,
                                   /*is_hex_id=*/false,
                                   /*ignore_special=*/true));

        CHECK_FALSE(repo->ReadTree("invalid_chars",
                                   check_symlinks,
                                   /*is_hex_id=*/true,
                                   /*ignore_special=*/true));

        CHECK_FALSE(repo->ReadTree(kFooId,
                                   check_symlinks,
                                   /*is_hex_id=*/true,
                                   /*ignore_special=*/true));
        CHECK_FALSE(repo->ReadTree(HexToRaw(kFooId),
                                   check_symlinks,
                                   /*is_hex_id=*/false,
                                   /*ignore_special=*/true));

        CHECK_FALSE(repo->ReadTree(kBarId,
                                   check_symlinks,
                                   /*is_hex_id=*/true,
                                   /*ignore_special=*/true));
        CHECK_FALSE(repo->ReadTree(HexToRaw(kBarId),
                                   check_symlinks,
                                   /*is_hex_id=*/false,
                                   /*ignore_special=*/true));
    }

    SECTION("valid trees") {
        auto entries0 = repo->ReadTree(kTreeSymId,
                                       check_symlinks,
                                       /*is_hex_id=*/true,
                                       /*ignore_special=*/true);
        auto entries1 = repo->ReadTree(HexToRaw(kTreeSymId),
                                       check_symlinks,
                                       /*is_hex_id=*/false,
                                       /*ignore_special=*/true);

        REQUIRE(entries0);
        REQUIRE(entries1);
        CHECK(*entries0 == *entries1);
    }
}

TEST_CASE("Read Git Trees with symlinks -- allow non-upwards", "[git_cas]") {
    auto repo_path = CreateTestRepoSymlinks(false);  // checkout needed
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);

    // create symlinks checker
    auto const check_symlinks = SymlinksChecker{cas};

    SECTION("invalid trees") {
        CHECK_FALSE(repo->ReadTree("", check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(repo->ReadTree("", check_symlinks, /*is_hex_id=*/false));

        CHECK_FALSE(
            repo->ReadTree(kFailId, check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(repo->ReadTree(
            HexToRaw(kFailId), check_symlinks, /*is_hex_id=*/false));

        CHECK_FALSE(repo->ReadTree(
            RawToHex("to_short"), check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(
            repo->ReadTree("to_short", check_symlinks, /*is_hex_id=*/false));

        CHECK_FALSE(repo->ReadTree(
            "invalid_chars", check_symlinks, /*is_hex_id=*/true));

        CHECK_FALSE(repo->ReadTree(kFooId, check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(repo->ReadTree(
            HexToRaw(kFooId), check_symlinks, /*is_hex_id=*/false));

        CHECK_FALSE(repo->ReadTree(kBarId, check_symlinks, /*is_hex_id=*/true));
        CHECK_FALSE(repo->ReadTree(
            HexToRaw(kBarId), check_symlinks, /*is_hex_id=*/false));
    }

    SECTION("valid trees") {
        auto entries0 =
            repo->ReadTree(kTreeSymId, check_symlinks, /*is_hex_id=*/true);
        auto entries1 = repo->ReadTree(
            HexToRaw(kTreeSymId), check_symlinks, /*is_hex_id=*/false);

        REQUIRE(entries0);
        REQUIRE(entries1);
        CHECK(*entries0 == *entries1);
    }
}

TEST_CASE("Create Git Trees", "[git_cas]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);

    // create symlinks checker
    auto const check_symlinks = SymlinksChecker{cas};

    SECTION("empty tree") {
        auto tree_id = repo->CreateTree({});
        REQUIRE(tree_id);
        CHECK(ToHexString(*tree_id) ==
              "4b825dc642cb6eb9a060e54bf8d69288fbee4904");
    }

    SECTION("existing tree") {
        auto entries =
            repo->ReadTree(kTreeId, check_symlinks, /*is_hex_id=*/true);
        REQUIRE(entries);

        auto tree_id = repo->CreateTree(*entries);
        REQUIRE(tree_id);
        CHECK(ToHexString(*tree_id) == kTreeId);
    }

    SECTION("entry order") {
        auto foo_bar = GitRepo::tree_entries_t{
            {HexToRaw(kFooId),
             {GitRepo::TreeEntry{"foo", ObjectType::File},
              GitRepo::TreeEntry{"bar", ObjectType::Executable}}}};
        auto foo_bar_id = repo->CreateTree(foo_bar);
        REQUIRE(foo_bar_id);

        auto bar_foo = GitRepo::tree_entries_t{
            {HexToRaw(kFooId),
             {GitRepo::TreeEntry{"bar", ObjectType::Executable},
              GitRepo::TreeEntry{"foo", ObjectType::File}}}};
        auto bar_foo_id = repo->CreateTree(bar_foo);
        REQUIRE(bar_foo_id);

        CHECK(foo_bar_id == bar_foo_id);
    }
}

TEST_CASE("Create Git Trees with symlinks", "[git_cas]") {
    auto repo_path = CreateTestRepoSymlinks(false);  // checkout needed
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);

    // create symlinks checker
    auto const check_symlinks = SymlinksChecker{cas};

    SECTION("existing tree with symlinks -- ignore special") {
        auto entries = repo->ReadTree(kTreeSymId,
                                      check_symlinks,
                                      /*is_hex_id=*/true,
                                      /*ignore_special=*/true);
        REQUIRE(entries);

        auto tree_id = repo->CreateTree(*entries);
        REQUIRE(tree_id);
        // if at least one symlink exists, it gets ignored and the tree id will
        // not match as it is NOT recomputed!
        CHECK_FALSE(ToHexString(*tree_id) == kTreeSymId);
    }

    SECTION("existing tree with symlinks -- allow non-upwards") {
        auto entries =
            repo->ReadTree(kTreeSymId, check_symlinks, /*is_hex_id=*/true);
        REQUIRE(entries);

        auto tree_id = repo->CreateTree(*entries);
        REQUIRE(tree_id);
        // all the symlinks in the test repo are non-upwards, so the tree should
        // be recreated exactly and id should thus match
        CHECK(ToHexString(*tree_id) == kTreeSymId);
    }
}

TEST_CASE("Read Git Tree Data", "[git_cas]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);

    // create symlinks checker
    auto const check_symlinks = SymlinksChecker{cas};

    SECTION("empty tree") {
        auto entries =
            GitRepo::ReadTreeData("",
                                  "4b825dc642cb6eb9a060e54bf8d69288fbee4904",
                                  check_symlinks,
                                  /*is_hex_id=*/true);
        REQUIRE(entries);
        CHECK(entries->empty());
    }

    SECTION("existing tree") {
        auto entries =
            repo->ReadTree(kTreeId, check_symlinks, /*is_hex_id=*/true);
        REQUIRE(entries);

        auto data = cas->ReadObject(kTreeId, /*is_hex_id=*/true);
        REQUIRE(data);

        auto from_data = GitRepo::ReadTreeData(
            *data, kTreeId, check_symlinks, /*is_hex_id=*/true);
        REQUIRE(from_data);
        CHECK(*from_data == *entries);
    }
}

TEST_CASE("Read Git Tree Data with non-upwards symlinks", "[git_cas]") {
    auto repo_path = CreateTestRepoSymlinks(false);  // checkout needed
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);

    // create symlinks checker
    auto const check_symlinks = SymlinksChecker{cas};

    SECTION("empty tree") {
        auto entries =
            GitRepo::ReadTreeData("",
                                  "4b825dc642cb6eb9a060e54bf8d69288fbee4904",
                                  check_symlinks,
                                  /*is_hex_id=*/true);
        REQUIRE(entries);
        CHECK(entries->empty());
    }

    SECTION("existing tree") {
        auto entries =
            repo->ReadTree(kTreeSymId, check_symlinks, /*is_hex_id=*/true);
        REQUIRE(entries);

        auto data = cas->ReadObject(kTreeSymId, /*is_hex_id=*/true);
        REQUIRE(data);

        auto from_data = GitRepo::ReadTreeData(
            *data, kTreeSymId, check_symlinks, /*is_hex_id=*/true);
        REQUIRE(from_data);
        CHECK(*from_data == *entries);
    }
}

TEST_CASE("Create Shallow Git Trees", "[git_cas]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);

    // create symlinks checker
    auto const check_symlinks = SymlinksChecker{cas};

    SECTION("empty tree") {
        auto tree = GitRepo::CreateShallowTree({});
        REQUIRE(tree);
        CHECK(ToHexString(tree->first) ==
              "4b825dc642cb6eb9a060e54bf8d69288fbee4904");
        CHECK(tree->second.empty());
    }

    SECTION("existing tree from other CAS") {
        auto entries =
            repo->ReadTree(kTreeId, check_symlinks, /*is_hex_id=*/true);
        REQUIRE(entries);

        auto tree = GitRepo::CreateShallowTree(*entries);
        REQUIRE(tree);
        CHECK(ToHexString(tree->first) == kTreeId);
        CHECK_FALSE(tree->second.empty());
    }
}

TEST_CASE("Read Git Tree", "[git_tree]") {
    SECTION("Bare repository") {
        auto repo_path = CreateTestRepo(true);
        REQUIRE(repo_path);
        CHECK(GitTree::Read(*repo_path, kTreeId));
        CHECK_FALSE(GitTree::Read(*repo_path, "wrong_tree_id"));
    }

    SECTION("Non-bare repository") {
        auto repo_path = CreateTestRepo(false);
        REQUIRE(repo_path);
        CHECK(GitTree::Read(*repo_path, kTreeId));
        CHECK_FALSE(GitTree::Read(*repo_path, "wrong_tree_id"));
    }
}

TEST_CASE("Read Git Tree with non-upwards symlinks", "[git_tree]") {
    auto repo_path = CreateTestRepoSymlinks(false);  // checkout needed
    REQUIRE(repo_path);
    CHECK(GitTree::Read(*repo_path, kTreeSymId));
    CHECK_FALSE(GitTree::Read(*repo_path, "wrong_tree_id"));
}

TEST_CASE("Lookup entries by name", "[git_tree]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto tree_root = GitTree::Read(*repo_path, kTreeId);
    REQUIRE(tree_root);

    auto entry_foo = tree_root->LookupEntryByName("foo");
    REQUIRE(entry_foo);
    CHECK(entry_foo->IsBlob());
    CHECK(entry_foo->Type() == ObjectType::File);

    auto blob_foo = entry_foo->Blob();
    REQUIRE(blob_foo);
    CHECK(*blob_foo == "foo");
    CHECK(blob_foo->size() == 3);
    CHECK(blob_foo->size() == *entry_foo->Size());

    auto entry_bar = tree_root->LookupEntryByName("bar");
    REQUIRE(entry_bar);
    CHECK(entry_bar->IsBlob());
    CHECK(entry_bar->Type() == ObjectType::Executable);

    auto blob_bar = entry_bar->Blob();
    REQUIRE(blob_bar);
    CHECK(*blob_bar == "bar");
    CHECK(blob_bar->size() == 3);
    CHECK(blob_bar->size() == *entry_bar->Size());

    auto entry_baz = tree_root->LookupEntryByName("baz");
    REQUIRE(entry_baz);
    CHECK(entry_baz->IsTree());
    CHECK(entry_baz->Type() == ObjectType::Tree);

    SECTION("Lookup missing entries") {
        CHECK_FALSE(tree_root->LookupEntryByName("fool"));
        CHECK_FALSE(tree_root->LookupEntryByName("barn"));
        CHECK_FALSE(tree_root->LookupEntryByName("bazel"));
    }

    SECTION("Lookup entries in sub-tree") {
        auto const& tree_baz = entry_baz->Tree();
        REQUIRE(tree_baz);

        auto entry_baz_foo = tree_baz->LookupEntryByName("foo");
        REQUIRE(entry_baz_foo);
        CHECK(entry_baz_foo->IsBlob());
        CHECK(entry_baz_foo->Hash() == entry_foo->Hash());

        auto entry_baz_bar = tree_baz->LookupEntryByName("bar");
        REQUIRE(entry_baz_bar);
        CHECK(entry_baz_bar->IsBlob());
        CHECK(entry_baz_bar->Hash() == entry_bar->Hash());

        SECTION("Lookup missing entries") {
            CHECK_FALSE(tree_baz->LookupEntryByName("fool"));
            CHECK_FALSE(tree_baz->LookupEntryByName("barn"));
            CHECK_FALSE(tree_baz->LookupEntryByName("bazel"));
        }
    }
}

TEST_CASE("Lookup symlinks by name", "[git_tree]") {
    auto repo_path = CreateTestRepoSymlinks(true);
    REQUIRE(repo_path);
    auto tree_root = GitTree::Read(*repo_path, kTreeSymId);
    REQUIRE(tree_root);

    auto entry_foo_l = tree_root->LookupEntryByName("foo_l");
    REQUIRE(entry_foo_l);
    CHECK(entry_foo_l->IsBlob());
    CHECK(entry_foo_l->Type() == ObjectType::Symlink);

    auto blob_foo_l = entry_foo_l->Blob();
    REQUIRE(blob_foo_l);
    CHECK(*blob_foo_l == "baz/foo");
    CHECK(blob_foo_l->size() == 7);
    CHECK(blob_foo_l->size() == *entry_foo_l->Size());

    auto entry_baz_l = tree_root->LookupEntryByName("baz_l");
    REQUIRE(entry_baz_l);
    CHECK(entry_baz_l->IsBlob());
    CHECK(entry_baz_l->Type() == ObjectType::Symlink);

    auto blob_baz_l = entry_baz_l->Blob();
    REQUIRE(blob_baz_l);
    CHECK(*blob_baz_l == "baz");
    CHECK(blob_baz_l->size() == 3);
    CHECK(blob_baz_l->size() == *entry_baz_l->Size());

    SECTION("Lookup missing entries") {
        CHECK_FALSE(tree_root->LookupEntryByName("fool"));
        CHECK_FALSE(tree_root->LookupEntryByName("barn"));
        CHECK_FALSE(tree_root->LookupEntryByName("bazel"));
    }

    SECTION("Lookup symlinks in sub-tree") {
        auto entry_baz = tree_root->LookupEntryByName("baz");
        REQUIRE(entry_baz);
        CHECK(entry_baz->IsTree());
        CHECK(entry_baz->Type() == ObjectType::Tree);

        auto const& tree_baz = entry_baz->Tree();
        REQUIRE(tree_baz);

        auto entry_baz_bar = tree_baz->LookupEntryByName("bar");
        REQUIRE(entry_baz_bar);
        CHECK(entry_baz_bar->IsBlob());
        CHECK(entry_baz_bar->Type() == ObjectType::Executable);

        auto entry_baz_bar_l = tree_baz->LookupEntryByName("bar_l");
        REQUIRE(entry_baz_bar_l);
        CHECK(entry_baz_bar_l->IsBlob());
        CHECK(entry_baz_bar_l->Type() == ObjectType::Symlink);
        // the hash of the symlink content should be the same as the file
        CHECK(entry_baz_bar_l->Hash() == entry_baz_bar->Hash());

        SECTION("Lookup missing entries") {
            CHECK_FALSE(tree_baz->LookupEntryByName("fool"));
            CHECK_FALSE(tree_baz->LookupEntryByName("barn"));
            CHECK_FALSE(tree_baz->LookupEntryByName("bazel"));
        }
    }
}

TEST_CASE("Lookup entries by path", "[git_tree]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto tree_root = GitTree::Read(*repo_path, kTreeId);
    REQUIRE(tree_root);

    auto entry_foo = tree_root->LookupEntryByPath("foo");
    REQUIRE(entry_foo);
    CHECK(entry_foo->IsBlob());
    CHECK(entry_foo->Type() == ObjectType::File);

    auto blob_foo = entry_foo->Blob();
    REQUIRE(blob_foo);
    CHECK(*blob_foo == "foo");
    CHECK(blob_foo->size() == 3);
    CHECK(blob_foo->size() == *entry_foo->Size());

    auto entry_bar = tree_root->LookupEntryByPath("bar");
    REQUIRE(entry_bar);
    CHECK(entry_bar->IsBlob());
    CHECK(entry_bar->Type() == ObjectType::Executable);

    auto blob_bar = entry_bar->Blob();
    REQUIRE(blob_bar);
    CHECK(*blob_bar == "bar");
    CHECK(blob_bar->size() == 3);
    CHECK(blob_bar->size() == *entry_bar->Size());

    auto entry_baz = tree_root->LookupEntryByPath("baz");
    REQUIRE(entry_baz);
    CHECK(entry_baz->IsTree());
    CHECK(entry_baz->Type() == ObjectType::Tree);

    SECTION("Lookup missing entries") {
        CHECK_FALSE(tree_root->LookupEntryByPath("fool"));
        CHECK_FALSE(tree_root->LookupEntryByPath("barn"));
        CHECK_FALSE(tree_root->LookupEntryByPath("bazel"));
    }

    SECTION("Lookup entries in sub-tree") {
        auto entry_baz_foo = tree_root->LookupEntryByPath("baz/foo");
        REQUIRE(entry_baz_foo);
        CHECK(entry_baz_foo->IsBlob());
        CHECK(entry_baz_foo->Hash() == entry_foo->Hash());

        auto entry_baz_bar = tree_root->LookupEntryByPath("baz/bar");
        REQUIRE(entry_baz_bar);
        CHECK(entry_baz_bar->IsBlob());
        CHECK(entry_baz_bar->Hash() == entry_bar->Hash());

        SECTION("Lookup missing entries") {
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/fool"));
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/barn"));
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/bazel"));
        }
    }
}

TEST_CASE("Lookup symlinks by path", "[git_tree]") {
    auto repo_path = CreateTestRepoSymlinks(true);
    REQUIRE(repo_path);
    auto tree_root = GitTree::Read(*repo_path, kTreeSymId);
    REQUIRE(tree_root);

    auto entry_foo_l = tree_root->LookupEntryByPath("foo_l");
    REQUIRE(entry_foo_l);
    CHECK(entry_foo_l->IsBlob());
    CHECK(entry_foo_l->Type() == ObjectType::Symlink);

    auto blob_foo_l = entry_foo_l->Blob();
    REQUIRE(blob_foo_l);
    CHECK(*blob_foo_l == "baz/foo");
    CHECK(blob_foo_l->size() == 7);
    CHECK(blob_foo_l->size() == *entry_foo_l->Size());

    auto entry_baz_l = tree_root->LookupEntryByPath("baz_l");
    REQUIRE(entry_baz_l);
    CHECK(entry_baz_l->IsBlob());
    CHECK(entry_baz_l->Type() == ObjectType::Symlink);

    auto blob_baz_l = entry_baz_l->Blob();
    REQUIRE(blob_baz_l);
    CHECK(*blob_baz_l == "baz");
    CHECK(blob_baz_l->size() == 3);
    CHECK(blob_baz_l->size() == *entry_baz_l->Size());

    SECTION("Lookup missing entries") {
        CHECK_FALSE(tree_root->LookupEntryByPath("fool"));
        CHECK_FALSE(tree_root->LookupEntryByPath("barn"));
        CHECK_FALSE(tree_root->LookupEntryByPath("bazel"));
    }

    SECTION("Lookup symlinks in sub-tree") {
        auto entry_baz_bar = tree_root->LookupEntryByPath("baz/bar");
        REQUIRE(entry_baz_bar);
        CHECK(entry_baz_bar->IsBlob());
        CHECK(entry_baz_bar->Type() == ObjectType::Executable);

        auto entry_baz_bar_l = tree_root->LookupEntryByPath("baz/bar_l");
        REQUIRE(entry_baz_bar_l);
        CHECK(entry_baz_bar_l->IsBlob());
        CHECK(entry_baz_bar_l->Type() == ObjectType::Symlink);
        // the hash of the symlink content should be the same as the file
        CHECK(entry_baz_bar_l->Hash() == entry_baz_bar->Hash());

        SECTION("Lookup missing entries") {
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/fool"));
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/barn"));
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/bazel"));
        }
    }
}

TEST_CASE("Lookup entries by special names", "[git_tree]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto tree_root = GitTree::Read(*repo_path, kTreeId);
    REQUIRE(tree_root);

    CHECK_FALSE(tree_root->LookupEntryByName("."));        // forbidden
    CHECK_FALSE(tree_root->LookupEntryByName(".."));       // forbidden
    CHECK_FALSE(tree_root->LookupEntryByName("baz/"));     // invalid name
    CHECK_FALSE(tree_root->LookupEntryByName("baz/foo"));  // invalid name
}

TEST_CASE("Lookup entries by special paths", "[git_tree]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto tree_root = GitTree::Read(*repo_path, kTreeId);
    REQUIRE(tree_root);

    SECTION("valid paths") {
        CHECK(tree_root->LookupEntryByPath("baz/"));
        CHECK(tree_root->LookupEntryByPath("baz/foo"));
        CHECK(tree_root->LookupEntryByPath("baz/../baz/"));
        CHECK(tree_root->LookupEntryByPath("./baz/"));
        CHECK(tree_root->LookupEntryByPath("./baz/foo"));
        CHECK(tree_root->LookupEntryByPath("./baz/../foo"));
    }

    SECTION("invalid paths") {
        CHECK_FALSE(tree_root->LookupEntryByPath("."));       // forbidden
        CHECK_FALSE(tree_root->LookupEntryByPath(".."));      // outside of tree
        CHECK_FALSE(tree_root->LookupEntryByPath("/baz"));    // outside of tree
        CHECK_FALSE(tree_root->LookupEntryByPath("baz/.."));  // == '.'
    }
}

TEST_CASE("Iterate tree entries", "[git_tree]") {
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto tree_root = GitTree::Read(*repo_path, kTreeId);
    REQUIRE(tree_root);

    std::vector<std::string> names{};
    for (auto const& [name, entry] : *tree_root) {
        names.emplace_back(name);
    }
    CHECK_THAT(names,
               HasSameUniqueElementsAs<std::vector<std::string>>(
                   {"foo", "bar", "baz"}));
}

TEST_CASE("Iterate tree entries with non-upwards symlinks", "[git_tree]") {
    auto repo_path = CreateTestRepoSymlinks(true);
    REQUIRE(repo_path);
    auto tree_root = GitTree::Read(*repo_path, kTreeSymId);
    REQUIRE(tree_root);

    std::vector<std::string> names{};
    for (auto const& [name, entry] : *tree_root) {
        names.emplace_back(name);
    }
    CHECK_THAT(names,
               HasSameUniqueElementsAs<std::vector<std::string>>(
                   {"foo", "bar", "baz", "foo_l", "baz_l"}));
}

TEST_CASE("Thread-safety", "[git_tree]") {
    constexpr auto kNumThreads = 100;

    atomic<bool> starting_signal{false};
    std::vector<std::thread> threads{};
    threads.reserve(kNumThreads);

    auto repo_path = CreateTestRepoSymlinks(false);  // checkout needed
    REQUIRE(repo_path);

    SECTION("Opening and reading from the same CAS") {
        for (int id{}; id < kNumThreads; ++id) {
            threads.emplace_back(
                [&repo_path, &starting_signal](int tid) {
                    starting_signal.wait(false);

                    auto cas = GitCAS::Open(*repo_path);
                    REQUIRE(cas);

                    // every second thread reads bar instead of foo
                    auto id = tid % 2 == 0 ? kFooId : kBarId;
                    CHECK(cas->ReadObject(id, /*is_hex_id=*/true));

                    auto header = cas->ReadHeader(id, /*is_hex_id=*/true);
                    CHECK(header->first == 3);
                    CHECK(header->second == ObjectType::File);
                },
                id);
        }

        starting_signal = true;
        starting_signal.notify_all();

        // wait for threads to finish
        for (auto& thread : threads) {
            thread.join();
        }
    }

    SECTION("Parsing same tree with same CAS") {
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        // create symlinks checker
        auto const check_symlinks = SymlinksChecker{cas};

        for (int id{}; id < kNumThreads; ++id) {
            threads.emplace_back([&cas, &starting_signal, check_symlinks]() {
                starting_signal.wait(false);

                auto repo = GitRepo::Open(cas);
                REQUIRE(repo);

                auto entries = repo->ReadTree(
                    kTreeSymId, check_symlinks, /*is_hex_id=*/true);
                REQUIRE(entries);
            });
        }

        starting_signal = true;
        starting_signal.notify_all();

        // wait for threads to finish
        for (auto& thread : threads) {
            thread.join();
        }
    }

    SECTION("Reading from different trees with same CAS") {
        for (int id{}; id < kNumThreads; ++id) {
            threads.emplace_back(
                [&repo_path, &starting_signal](int tid) {
                    starting_signal.wait(false);

                    auto tree_root = GitTree::Read(*repo_path, kTreeSymId);
                    REQUIRE(tree_root);

                    auto entry_subdir = tree_root->LookupEntryByName("baz");
                    REQUIRE(entry_subdir);
                    REQUIRE(entry_subdir->IsTree());

                    // every second thread reads subdir instead of root
                    auto const& tree_read =
                        tid % 2 == 0 ? tree_root : entry_subdir->Tree();

                    auto entry_foo = tree_read->LookupEntryByName("foo");
                    auto entry_bar = tree_read->LookupEntryByName("bar");
                    REQUIRE(entry_foo);
                    REQUIRE(entry_bar);
                    CHECK(entry_foo->Blob() == "foo");
                    CHECK(entry_bar->Blob() == "bar");
                },
                id);
        }

        starting_signal = true;
        starting_signal.notify_all();

        // wait for threads to finish
        for (auto& thread : threads) {
            thread.join();
        }
    }

    SECTION("Reading from the same tree") {
        auto tree_root = GitTree::Read(*repo_path, kTreeSymId);
        REQUIRE(tree_root);

        for (int id{}; id < kNumThreads; ++id) {
            threads.emplace_back(
                [&tree_root, &starting_signal](int tid) {
                    // every second thread reads bar instead of foo
                    auto name =
                        tid % 2 == 0 ? std::string{"foo"} : std::string{"bar"};

                    starting_signal.wait(false);

                    auto entry = tree_root->LookupEntryByName(name);
                    REQUIRE(entry);
                    CHECK(entry->Blob() == name);
                },
                id);
        }

        starting_signal = true;
        starting_signal.notify_all();

        // wait for threads to finish
        for (auto& thread : threads) {
            thread.join();
        }
    }
}
