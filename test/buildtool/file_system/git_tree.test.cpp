#include <thread>

#include "catch2/catch.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_tree.hpp"
#include "test/utils/container_matchers.hpp"

namespace {

auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo.bundle"};
auto const kTreeId = std::string{"e51a219a27b672ccf17abec7d61eb4d6e0424140"};
auto const kFooId = std::string{"19102815663d23f8b75a47e7a01965dcdc96468c"};
auto const kBarId = std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};
auto const kFailId = std::string{"0123456789abcdef0123456789abcdef01234567"};

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
                           kBundlePath,
                           repo_path.string());
    if (std::system(cmd.c_str()) == 0) {
        return repo_path;
    }
    return std::nullopt;
}

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
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);

    SECTION("valid ids") {
        CHECK(cas->ReadObject(kFooId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kFooId), /*is_hex_id=*/false));

        CHECK(cas->ReadObject(kBarId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kBarId), /*is_hex_id=*/false));

        CHECK(cas->ReadObject(kTreeId, /*is_hex_id=*/true));
        CHECK(cas->ReadObject(HexToRaw(kTreeId), /*is_hex_id=*/false));
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
    auto repo_path = CreateTestRepo(true);
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);

    SECTION("valid ids") {
        CHECK(cas->ReadHeader(kFooId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kFooId), /*is_hex_id=*/false));

        CHECK(cas->ReadHeader(kBarId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kBarId), /*is_hex_id=*/false));

        CHECK(cas->ReadHeader(kTreeId, /*is_hex_id=*/true));
        CHECK(cas->ReadHeader(HexToRaw(kTreeId), /*is_hex_id=*/false));
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

    SECTION("invalid trees") {
        CHECK_FALSE(cas->ReadTree("", /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadTree("", /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadTree(kFailId, /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadTree(HexToRaw(kFailId), /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadTree(RawToHex("to_short"), /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadTree("to_short", /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadTree("invalid_chars", /*is_hex_id=*/true));

        CHECK_FALSE(cas->ReadTree(kFooId, /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadTree(HexToRaw(kFooId), /*is_hex_id=*/false));

        CHECK_FALSE(cas->ReadTree(kBarId, /*is_hex_id=*/true));
        CHECK_FALSE(cas->ReadTree(HexToRaw(kBarId), /*is_hex_id=*/false));
    }

    SECTION("valid trees") {
        auto entries0 = cas->ReadTree(kTreeId, /*is_hex_id=*/true);
        auto entries1 = cas->ReadTree(HexToRaw(kTreeId), /*is_hex_id=*/false);
        REQUIRE(entries0);
        REQUIRE(entries1);
        CHECK(*entries0 == *entries1);
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

        auto entry_baz_baz = tree_baz->LookupEntryByName("baz");
        REQUIRE(entry_baz_baz);
        CHECK(entry_baz_baz->IsTree());

        SECTION("Lookup missing entries") {
            CHECK_FALSE(tree_baz->LookupEntryByName("fool"));
            CHECK_FALSE(tree_baz->LookupEntryByName("barn"));
            CHECK_FALSE(tree_baz->LookupEntryByName("bazel"));
        }

        SECTION("Lookup entries in sub-sub-tree") {
            auto const& tree_baz_baz = entry_baz_baz->Tree();
            REQUIRE(tree_baz_baz);

            auto entry_baz_baz_foo = tree_baz_baz->LookupEntryByName("foo");
            REQUIRE(entry_baz_baz_foo);
            CHECK(entry_baz_baz_foo->IsBlob());
            CHECK(entry_baz_baz_foo->Hash() == entry_foo->Hash());

            auto entry_baz_baz_bar = tree_baz_baz->LookupEntryByName("bar");
            REQUIRE(entry_baz_baz_bar);
            CHECK(entry_baz_baz_bar->IsBlob());
            CHECK(entry_baz_baz_bar->Hash() == entry_bar->Hash());

            SECTION("Lookup missing entries") {
                CHECK_FALSE(tree_baz_baz->LookupEntryByName("fool"));
                CHECK_FALSE(tree_baz_baz->LookupEntryByName("barn"));
                CHECK_FALSE(tree_baz_baz->LookupEntryByName("bazel"));
            }
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

        auto entry_baz_baz = tree_root->LookupEntryByPath("baz/baz");
        REQUIRE(entry_baz_baz);
        CHECK(entry_baz_baz->IsTree());

        SECTION("Lookup missing entries") {
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/fool"));
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/barn"));
            CHECK_FALSE(tree_root->LookupEntryByPath("baz/bazel"));
        }

        SECTION("Lookup entries in sub-sub-tree") {
            auto entry_baz_baz_foo =
                tree_root->LookupEntryByPath("baz/baz/foo");
            REQUIRE(entry_baz_baz_foo);
            CHECK(entry_baz_baz_foo->IsBlob());
            CHECK(entry_baz_baz_foo->Hash() == entry_foo->Hash());

            auto entry_baz_baz_bar =
                tree_root->LookupEntryByPath("baz/baz/bar");
            REQUIRE(entry_baz_baz_bar);
            CHECK(entry_baz_baz_bar->IsBlob());
            CHECK(entry_baz_baz_bar->Hash() == entry_bar->Hash());

            SECTION("Lookup missing entries") {
                CHECK_FALSE(tree_root->LookupEntryByPath("baz/baz/fool"));
                CHECK_FALSE(tree_root->LookupEntryByPath("baz/baz/barn"));
                CHECK_FALSE(tree_root->LookupEntryByPath("baz/baz/bazel"));
            }
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
        CHECK(entry);
        names.emplace_back(name);
    }
    CHECK_THAT(names,
               HasSameUniqueElementsAs<std::vector<std::string>>(
                   {"foo", "bar", "baz"}));
}

TEST_CASE("Thread-safety", "[git_tree]") {
    constexpr auto kNumThreads = 100;

    atomic<bool> starting_signal{false};
    std::vector<std::thread> threads{};
    threads.reserve(kNumThreads);

    auto repo_path = CreateTestRepo(true);
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

        for (int id{}; id < kNumThreads; ++id) {
            threads.emplace_back([&cas, &starting_signal]() {
                starting_signal.wait(false);

                auto entries = cas->ReadTree(kTreeId, true);
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

                    auto tree_root = GitTree::Read(*repo_path, kTreeId);
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
        auto tree_root = GitTree::Read(*repo_path, kTreeId);
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
