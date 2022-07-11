#include "catch2/catch.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "test/utils/hermeticity/local.hpp"

namespace {

[[nodiscard]] auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() / "test/buildtool/common";
}

[[nodiscard]] auto GetGitRoot() -> FileRoot {
    static std::atomic<int> counter{};
    auto repo_path =
        GetTestDir() / "test_repo" /
        std::filesystem::path{std::to_string(counter++)}.filename();
    REQUIRE(FileSystemManager::CreateDirectory(repo_path));
    auto anchor = FileSystemManager::ChangeDirectory(repo_path);
    if (std::system("git init") == 0 and
        std::system("git -c user.name='nobody' -c user.email='' "
                    "commit --allow-empty -m'init'") == 0) {
        auto constexpr kEmptyTreeId =
            "4b825dc642cb6eb9a060e54bf8d69288fbee4904";
        if (auto root = FileRoot::FromGit(repo_path, kEmptyTreeId)) {
            return std::move(*root);
        }
    }
    return FileRoot{"missing"};
}

[[nodiscard]] auto CreateFixedRepoInfo(
    std::map<std::string, std::string> const& bindings = {},
    std::string const& tfn = "TARGETS",
    std::string const& rfn = "RULES",
    std::string const& efn = "EXPRESSIONS") {
    static auto const kGitRoot = GetGitRoot();
    return RepositoryConfig::RepositoryInfo{
        kGitRoot, kGitRoot, kGitRoot, kGitRoot, bindings, tfn, rfn, efn};
}

[[nodiscard]] auto CreateFileRepoInfo(
    std::map<std::string, std::string> const& bindings = {},
    std::string const& tfn = "TARGETS",
    std::string const& rfn = "RULES",
    std::string const& efn = "EXPRESSIONS") {
    static auto const kFileRoot = FileRoot{"file path"};
    return RepositoryConfig::RepositoryInfo{
        kFileRoot, kFileRoot, kFileRoot, kFileRoot, bindings, tfn, rfn, efn};
}

template <class T>
[[nodiscard]] auto Copy(T const& x) -> T {
    return x;
}

// Read graph from CAS
[[nodiscard]] auto ReadGraph(std::string const& hash) -> nlohmann::json {
    auto& cas = LocalCAS<ObjectType::File>::Instance();
    auto blob = cas.BlobPath(
        ArtifactDigest{hash, /*does not matter*/ 0, /*is_tree=*/false});
    REQUIRE(blob);
    auto content = FileSystemManager::ReadFile(*blob);
    REQUIRE(content);
    return nlohmann::json::parse(*content);
}

// From [info0, info1, ...] and [bindings0, bindings1, ...]
// build graph: {"0": (info0 + bindings0), "1": (info1 + bindings1), ...}
[[nodiscard]] auto BuildGraph(
    std::vector<RepositoryConfig::RepositoryInfo const*> const& infos,
    std::vector<std::unordered_map<std::string, std::string>> const& bindings)
    -> nlohmann::json {
    auto graph = nlohmann::json::object();
    for (std::size_t i{}; i < infos.size(); ++i) {
        auto entry = infos[i]->BaseContentDescription();
        REQUIRE(entry);
        (*entry)["bindings"] = bindings[i];
        graph[std::to_string(i)] = std::move(*entry);
    }
    return graph;
}

}  // namespace

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Test missing repository",
                 "[repository_config]") {
    auto& config = RepositoryConfig::Instance();
    config.Reset();

    CHECK(config.Info("missing") == nullptr);
    CHECK_FALSE(config.RepositoryKey("missing"));
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Compute key of fixed repository",
                 "[repository_config]") {
    auto& config = RepositoryConfig::Instance();
    config.Reset();

    SECTION("for single fixed repository") {
        config.SetInfo("foo", CreateFixedRepoInfo());
        auto key = config.RepositoryKey("foo");
        REQUIRE(key);

        // verify created graph from CAS
        CHECK(ReadGraph(*key) == BuildGraph({config.Info("foo")}, {{}}));
    }

    SECTION("for fixed repositories with same missing dependency") {
        config.SetInfo("foo", CreateFixedRepoInfo({{"dep", "baz"}}));
        config.SetInfo("bar", CreateFixedRepoInfo({{"dep", "baz"}}));
        CHECK_FALSE(config.RepositoryKey("foo"));
        CHECK_FALSE(config.RepositoryKey("bar"));
    }

    SECTION("for fixed repositories with different missing dependency") {
        config.SetInfo("foo", CreateFixedRepoInfo({{"dep", "baz0"}}));
        config.SetInfo("bar", CreateFixedRepoInfo({{"dep", "baz1"}}));
        CHECK_FALSE(config.RepositoryKey("foo"));
        CHECK_FALSE(config.RepositoryKey("bar"));
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Compute key of file repository",
                 "[repository_config]") {
    auto& config = RepositoryConfig::Instance();
    config.Reset();

    SECTION("for single file repository") {
        config.SetInfo("foo", CreateFileRepoInfo());
        CHECK_FALSE(config.RepositoryKey("foo"));
    }

    SECTION("for graph with leaf dependency as file") {
        config.SetInfo("foo", CreateFixedRepoInfo({{"bar", "bar"}}));
        config.SetInfo("bar", CreateFixedRepoInfo({{"baz", "baz"}}));
        config.SetInfo("baz", CreateFileRepoInfo());
        CHECK_FALSE(config.RepositoryKey("foo"));
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Compare key of two repos with same content",
                 "[repository_config]") {
    auto& config = RepositoryConfig::Instance();
    config.Reset();

    // create two different repo infos with same content (baz should be same)
    config.SetInfo("foo", CreateFixedRepoInfo({{"dep", "baz0"}}));
    config.SetInfo("bar", CreateFixedRepoInfo({{"dep", "baz1"}}));

    SECTION("with leaf dependency") {
        // create duplicate leaf repo info with global name 'baz0' and 'baz1'
        auto baz = CreateFixedRepoInfo();
        config.SetInfo("baz0", Copy(baz));
        config.SetInfo("baz1", Copy(baz));

        // check if computed key is same
        auto foo_key = config.RepositoryKey("foo");
        auto bar_key = config.RepositoryKey("bar");
        REQUIRE(foo_key);
        REQUIRE(bar_key);
        CHECK(*foo_key == *bar_key);

        // verify created graph from CAS
        CHECK(ReadGraph(*foo_key) ==
              BuildGraph({config.Info("foo"), config.Info("baz0")},
                         {{{"dep", "1"}}, {}}));
    }

    SECTION("with cyclic dependency") {
        // create duplicate cyclic repo info with global name 'baz0' and 'baz1'
        auto baz = CreateFixedRepoInfo({{"foo", "foo"}, {"bar", "bar"}});
        config.SetInfo("baz0", Copy(baz));
        config.SetInfo("baz1", Copy(baz));

        // check if computed key is same
        auto foo_key = config.RepositoryKey("foo");
        auto bar_key = config.RepositoryKey("bar");
        REQUIRE(foo_key);
        REQUIRE(bar_key);
        CHECK(*foo_key == *bar_key);

        // verify created graph from CAS
        CHECK(ReadGraph(*foo_key) ==
              BuildGraph({config.Info("foo"), config.Info("baz0")},
                         {{{"dep", "1"}}, {{"foo", "0"}, {"bar", "0"}}}));
    }

    SECTION("with two separate cyclic graphs") {
        // create two cyclic repo infos producing two separate graphs
        config.SetInfo("baz0", CreateFixedRepoInfo({{"dep", "foo"}}));
        config.SetInfo("baz1", CreateFixedRepoInfo({{"dep", "bar"}}));

        // check if computed key is same
        auto foo_key = config.RepositoryKey("foo");
        auto bar_key = config.RepositoryKey("bar");
        REQUIRE(foo_key);
        REQUIRE(bar_key);
        CHECK(*foo_key == *bar_key);

        // verify created graph from CAS
        CHECK(ReadGraph(*foo_key) ==
              BuildGraph({config.Info("foo")}, {{{"dep", "0"}}}));
    }

    SECTION("for graph with leaf repos refering to themselfs") {
        config.SetInfo("baz0", CreateFixedRepoInfo({{"dep", "baz0"}}));
        config.SetInfo("baz1", CreateFixedRepoInfo({{"dep", "baz1"}}));

        // check if computed key is same
        auto foo_key = config.RepositoryKey("foo");
        auto bar_key = config.RepositoryKey("bar");
        auto baz0_key = config.RepositoryKey("baz0");
        auto baz1_key = config.RepositoryKey("baz1");
        CHECK(foo_key);
        CHECK(bar_key);
        CHECK(baz0_key);
        CHECK(baz1_key);
        CHECK(*foo_key == *bar_key);
        CHECK(*bar_key == *baz0_key);
        CHECK(*baz0_key == *baz1_key);

        // verify created graph from CAS
        CHECK(ReadGraph(*foo_key) ==
              BuildGraph({config.Info("foo")}, {{{"dep", "0"}}}));
    }
}
