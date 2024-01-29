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

#include <thread>

#include "catch2/catch_test_macros.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/utils/cpp/atomic.hpp"
#include "test/utils/shell_quoting.hpp"

namespace {

auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo_symlinks.bundle"};
auto const kRootCommit =
    std::string{"3ecce3f5b19ad7941c6354d65d841590662f33ef"};
auto const kRootId = std::string{"18770dacfe14c15d88450c21c16668e13ab0e7f9"};
auto const kBazId = std::string{"1868f82682c290f0b1db3cacd092727eef1fa57f"};
auto const kFooId = std::string{"19102815663d23f8b75a47e7a01965dcdc96468c"};
auto const kBarId = std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};

}  // namespace

class TestUtils {
  public:
    [[nodiscard]] static auto GetTestDir() noexcept -> std::filesystem::path {
        auto* tmp_dir = std::getenv("TEST_TMPDIR");
        if (tmp_dir != nullptr) {
            return tmp_dir;
        }
        return FileSystemManager::GetCurrentDirectory() / "test/other_tools";
    }

    [[nodiscard]] static auto GetRepoPath() noexcept -> std::filesystem::path {
        return GetTestDir() / "test_git_repo" /
               std::filesystem::path{std::to_string(counter++)}.filename();
    }

    // The checkout will make the content available, as well as the HEAD ref
    [[nodiscard]] static auto CreateTestRepoWithCheckout(
        bool is_bare = false) noexcept -> std::optional<std::filesystem::path> {
        auto repo_path = CreateTestRepo(is_bare);
        REQUIRE(repo_path);
        auto cmd =
            fmt::format("git --git-dir={} --work-tree={} checkout master",
                        QuoteForShell(is_bare ? repo_path->string()
                                              : (*repo_path / ".git").string()),
                        QuoteForShell(repo_path->string()));
        if (std::system(cmd.c_str()) == 0) {
            return *repo_path;
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto CreateTestRepo(bool is_bare = false) noexcept
        -> std::optional<std::filesystem::path> {
        auto repo_path = GetRepoPath();
        auto cmd = fmt::format("git clone {}{} {}",
                               is_bare ? "--bare " : "",
                               QuoteForShell(kBundlePath),
                               QuoteForShell(repo_path.string()));
        if (std::system(cmd.c_str()) == 0) {
            return repo_path;
        }
        return std::nullopt;
    }

  private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static inline std::atomic<int> counter = 0;
};

TEST_CASE("Open Git repo", "[git_repo]") {
    SECTION("Fake bare repository") {
        auto repo_path = TestUtils::CreateTestRepo(true);
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepo::Open(cas);
        REQUIRE(repo);
        CHECK(repo->GetGitCAS() == cas);  // same odb, same GitCAS
        CHECK(repo->IsRepoFake());
    }

    SECTION("Fake non-bare repository") {
        auto repo_path = TestUtils::CreateTestRepo();
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepo::Open(cas);
        REQUIRE(repo);
        CHECK(repo->GetGitCAS() == cas);  // same odb, same GitCAS
        CHECK(repo->IsRepoFake());
    }

    SECTION("Real bare repository") {
        auto repo_path = TestUtils::CreateTestRepo(true);
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepo::Open(*repo_path);
        REQUIRE(repo);
        CHECK_FALSE(repo->GetGitCAS() == cas);  // same odb, different GitCAS
        CHECK_FALSE(repo->IsRepoFake());
    }

    SECTION("Real non-bare repository") {
        auto repo_path = TestUtils::CreateTestRepo();
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepo::Open(*repo_path);
        REQUIRE(repo);
        CHECK_FALSE(repo->GetGitCAS() == cas);  // same odb, different GitCAS
        CHECK_FALSE(repo->IsRepoFake());
    }

    SECTION("Non-existing repository") {
        auto repo = GitRepo::Open("does_not_exist");
        REQUIRE(repo == std::nullopt);
    }

    SECTION("Initialize and open bare repository") {
        auto repo_path = TestUtils::GetRepoPath();
        auto repo = GitRepo::InitAndOpen(repo_path, /*is_bare=*/true);
        REQUIRE(repo);
        CHECK_FALSE(repo->IsRepoFake());
    }

    SECTION("Real non-bare repository with checkout") {
        auto repo_path = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepo::Open(cas);
        REQUIRE(repo);
        CHECK(repo->GetGitCAS() == cas);
        CHECK(repo->IsRepoFake());
    }
}

TEST_CASE("Single-threaded real repository local operations", "[git_repo]") {
    // setup dummy logger
    auto logger = std::make_shared<GitRepo::anon_logger_t>(
        [](auto const& msg, bool fatal) {
            Logger::Log(fatal ? LogLevel::Error : LogLevel::Progress,
                        std::string(msg));
        });

    SECTION("Stage and commit all") {
        // make blank repo
        auto repo_commit_path = TestUtils::GetRepoPath();
        auto repo_commit =
            GitRepo::InitAndOpen(repo_commit_path, /*is_bare=*/false);
        REQUIRE(repo_commit);
        CHECK_FALSE(repo_commit->IsRepoFake());

        // add blank file
        REQUIRE(FileSystemManager::WriteFile(
            "test no 1", repo_commit_path / "test1.txt", true));
        REQUIRE(FileSystemManager::WriteFile(
            "test no 2", repo_commit_path / "test2.txt", true));

        // stage and commit all
        auto commit =
            repo_commit->StageAndCommitAllAnonymous("test commit", logger);
        CHECK(commit);
    }

    SECTION("Tag commit") {
        auto repo_tag_path = TestUtils::CreateTestRepo(true);
        REQUIRE(repo_tag_path);
        auto repo_tag = GitRepo::Open(*repo_tag_path);
        REQUIRE(repo_tag);
        CHECK_FALSE(repo_tag->IsRepoFake());

        CHECK(repo_tag->KeepTag(kRootCommit, "test tag", logger));
    }

    SECTION("Get head commit") {
        auto repo_wHead_path = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(repo_wHead_path);
        auto repo_wHead = GitRepo::Open(*repo_wHead_path);
        REQUIRE(repo_wHead);

        auto head_commit = repo_wHead->GetHeadCommit(logger);
        REQUIRE(head_commit);
        CHECK(*head_commit == kRootCommit);
    }

    SECTION("Fetch with base refspecs from path") {
        // make bare real repo to fetch into
        auto path_fetch_all = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(path_fetch_all);
        auto repo_fetch_all = GitRepo::Open(*path_fetch_all);

        // fetch all
        CHECK(repo_fetch_all->FetchFromPath(
            nullptr, *path_fetch_all, std::nullopt, logger));
    }

    SECTION("Fetch branch from path") {
        // make bare real repo to fetch into
        auto path_fetch_branch = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(path_fetch_branch);
        auto repo_fetch_branch = GitRepo::Open(*path_fetch_branch);
        REQUIRE(repo_fetch_branch);

        // fetch branch
        CHECK(repo_fetch_branch->FetchFromPath(
            nullptr, *path_fetch_branch, "master", logger));
    }
}

TEST_CASE("Single-threaded fake repository operations", "[git_repo]") {
    auto repo_path = TestUtils::CreateTestRepoWithCheckout();
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepo::Open(cas);
    REQUIRE(repo);
    REQUIRE(repo->GetGitCAS() == cas);
    REQUIRE(repo->IsRepoFake());

    // setup dummy logger
    auto logger = std::make_shared<GitRepo::anon_logger_t>(
        [](auto const& msg, bool fatal) {
            Logger::Log(fatal ? LogLevel::Error : LogLevel::Progress,
                        std::string(msg));
        });

    SECTION("Get subtree entry id from commit") {
        SECTION("Get base tree id") {
            auto entry_root_c =
                repo->GetSubtreeFromCommit(kRootCommit, ".", logger);
            REQUIRE(std::holds_alternative<std::string>(entry_root_c));
            CHECK(std::get<std::string>(entry_root_c) == kRootId);
        }

        SECTION("Get inner tree id") {
            auto entry_baz_c =
                repo->GetSubtreeFromCommit(kRootCommit, "baz", logger);
            REQUIRE(std::holds_alternative<std::string>(entry_baz_c));
            CHECK(std::get<std::string>(entry_baz_c) == kBazId);
        }
    }

    SECTION("Get subtree entry id from root tree id") {
        SECTION("Get base tree id") {
            auto entry_root_t = repo->GetSubtreeFromTree(kRootId, ".", logger);
            REQUIRE(entry_root_t);
            CHECK(*entry_root_t == kRootId);
        }

        SECTION("Get inner tree id") {
            auto entry_baz_t = repo->GetSubtreeFromTree(kRootId, "baz", logger);
            REQUIRE(entry_baz_t);
            CHECK(*entry_baz_t == kBazId);
        }
    }

    SECTION("Find repository root from path") {
        SECTION("Non-bare repository") {
            auto root_path = GitRepo::GetRepoRootFromPath(*repo_path, logger);
            REQUIRE(root_path);
            CHECK(*root_path == *repo_path);

            auto root_path_from_baz =
                GitRepo::GetRepoRootFromPath(*repo_path / "baz", logger);
            REQUIRE(root_path_from_baz);
            CHECK(*root_path_from_baz == *repo_path);

            auto root_path_from_bazfoo =
                GitRepo::GetRepoRootFromPath(*repo_path / "baz/foo", logger);
            REQUIRE(root_path_from_bazfoo);
            CHECK(*root_path_from_bazfoo == *repo_path);

            auto root_path_non_exist =
                GitRepo::GetRepoRootFromPath("does_not_exist", logger);
            REQUIRE(root_path_non_exist);
            CHECK(root_path_non_exist->empty());
        }

        SECTION("Bare repository") {
            auto bare_repo_path = TestUtils::CreateTestRepo(true);
            REQUIRE(bare_repo_path);
            auto bare_cas = GitCAS::Open(*bare_repo_path);
            REQUIRE(bare_cas);
            auto bare_repo = GitRepo::Open(bare_cas);
            REQUIRE(bare_repo);

            auto bare_repo_root_path =
                GitRepo::GetRepoRootFromPath(*bare_repo_path, logger);
            REQUIRE(bare_repo_root_path);
            CHECK(*bare_repo_root_path == *bare_repo_path);
        }
    }

    SECTION("Get subtree entry id from path") {
        SECTION("Get base tree id") {
            auto entry_root_p =
                repo->GetSubtreeFromPath(*repo_path, kRootCommit, logger);
            REQUIRE(entry_root_p);
            CHECK(*entry_root_p == kRootId);
        }

        SECTION("Get inner tree id") {
            auto path_baz = *repo_path / "baz";
            auto entry_baz_p =
                repo->GetSubtreeFromPath(path_baz, kRootCommit, logger);
            REQUIRE(entry_baz_p);
            CHECK(*entry_baz_p == kBazId);
        }
    }

    SECTION("Check if commit exists in repository") {
        SECTION("Repository containing commit") {
            auto path_containing = TestUtils::CreateTestRepo();
            REQUIRE(path_containing);
            auto cas_containing = GitCAS::Open(*path_containing);
            REQUIRE(cas_containing);
            auto repo_containing = GitRepo::Open(cas_containing);
            REQUIRE(repo_containing);

            auto result_containing =
                repo_containing->CheckCommitExists(kRootCommit, logger);
            CHECK(*result_containing);
        }

        SECTION("Repository not containing commit") {
            auto path_non_bare = TestUtils::GetRepoPath();
            {
                auto repo_tmp =
                    GitRepo::InitAndOpen(path_non_bare, /*is_bare=*/false);
                REQUIRE(repo_tmp);
            }
            auto cas_non_bare = GitCAS::Open(path_non_bare);
            REQUIRE(cas_non_bare);
            auto repo_non_bare = GitRepo::Open(cas_non_bare);
            REQUIRE(repo_non_bare);

            auto result_non_bare =
                repo_non_bare->CheckCommitExists(kRootCommit, logger);
            CHECK_FALSE(*result_non_bare);
        }
    }

    SECTION("Read object from tree by relative path") {
        SECTION("Non-existing") {
            auto obj_info =
                repo->GetObjectByPathFromTree(kRootId, "does_not_exist");
            CHECK_FALSE(obj_info);
        }
        SECTION("File") {
            auto obj_info = repo->GetObjectByPathFromTree(kRootId, "foo");
            REQUIRE(obj_info);
            CHECK(obj_info->id == kFooId);
            CHECK(obj_info->type == ObjectType::File);
            CHECK_FALSE(obj_info->symlink_content);
        }
        SECTION("Tree") {
            auto obj_info = repo->GetObjectByPathFromTree(kRootId, "baz");
            REQUIRE(obj_info);
            CHECK(obj_info->id == kBazId);
            CHECK(obj_info->type == ObjectType::Tree);
            CHECK_FALSE(obj_info->symlink_content);
        }
        SECTION("Symlink") {
            auto obj_info = repo->GetObjectByPathFromTree(kRootId, "baz/bar_l");
            REQUIRE(obj_info);
            CHECK(obj_info->id == kBarId);
            CHECK(obj_info->type == ObjectType::Symlink);
            CHECK(obj_info->symlink_content);
            CHECK(*obj_info->symlink_content == "bar");
        }
    }

    SECTION("Fetch from local repository via temporary repository") {
        SECTION("Fetch all") {
            // set repo to fetch into
            auto path_fetch_all = TestUtils::GetRepoPath();
            auto repo_fetch_all =
                GitRepo::InitAndOpen(path_fetch_all, /*is_bare=*/true);
            REQUIRE(repo_fetch_all);

            // check commit is not there before fetch
            CHECK_FALSE(
                *repo_fetch_all->CheckCommitExists(kRootCommit, logger));

            // create tmp dir to use for fetch
            auto tmp_path_fetch_all = TestUtils::GetRepoPath();
            REQUIRE(FileSystemManager::CreateDirectory(tmp_path_fetch_all));
            // fetch all with base refspecs
            REQUIRE(repo_fetch_all->LocalFetchViaTmpRepo(
                tmp_path_fetch_all, *repo_path, std::nullopt, logger));

            // check commit is there after fetch
            CHECK(*repo_fetch_all->CheckCommitExists(kRootCommit, logger));
        }

        SECTION("Fetch branch") {
            // set repo to fetch into
            auto path_fetch_branch = TestUtils::GetRepoPath();
            auto repo_fetch_branch =
                GitRepo::InitAndOpen(path_fetch_branch, /*is_bare=*/true);
            REQUIRE(repo_fetch_branch);

            // check commit is not there before fetch
            CHECK_FALSE(
                *repo_fetch_branch->CheckCommitExists(kRootCommit, logger));

            // create tmp dir to use for fetch
            auto tmp_path_fetch_branch = TestUtils::GetRepoPath();
            REQUIRE(FileSystemManager::CreateDirectory(tmp_path_fetch_branch));
            // fetch branch
            REQUIRE(repo_fetch_branch->LocalFetchViaTmpRepo(
                tmp_path_fetch_branch, *repo_path, "master", logger));

            // check commit is there after fetch
            CHECK(*repo_fetch_branch->CheckCommitExists(kRootCommit, logger));
        }
    }
}

TEST_CASE("Multi-threaded fake repository operations", "[git_repo]") {
    /*
    Test all fake repository operations while being done in parallel.
    They are supposed to be thread-safe, so no conflicts should exist.
    */
    // define remote, for ops that need it
    auto remote_repo_path = TestUtils::CreateTestRepoWithCheckout();
    REQUIRE(remote_repo_path);
    auto remote_cas = GitCAS::Open(*remote_repo_path);
    REQUIRE(remote_cas);

    // setup dummy logger
    auto logger = std::make_shared<GitRepo::anon_logger_t>(
        [](auto const& msg, bool fatal) {
            Logger::Log(fatal ? LogLevel::Error : LogLevel::Progress,
                        std::string(msg));
        });

    // setup threading
    constexpr auto kNumThreads = 100;

    atomic<bool> starting_signal{false};
    std::vector<std::thread> threads{};
    threads.reserve(kNumThreads);

    SECTION("Lookups in the same ODB") {
        constexpr int NUM_CASES = 6;
        for (int id{}; id < kNumThreads; ++id) {
            threads.emplace_back(
                [&remote_cas, &remote_repo_path, &logger, &starting_signal](
                    int tid) {
                    starting_signal.wait(false);
                    // cases based on thread number
                    switch (tid % NUM_CASES) {
                        case 0: {
                            auto remote_repo = GitRepo::Open(remote_cas);
                            REQUIRE(remote_repo);
                            REQUIRE(remote_repo->IsRepoFake());
                            // Get subtree entry id from commit
                            auto entry_baz_c =
                                remote_repo->GetSubtreeFromCommit(
                                    kRootCommit, "baz", logger);
                            REQUIRE(std::holds_alternative<std::string>(
                                entry_baz_c));
                            CHECK(std::get<std::string>(entry_baz_c) == kBazId);
                        } break;
                        case 1: {
                            auto remote_repo = GitRepo::Open(remote_cas);
                            REQUIRE(remote_repo);
                            REQUIRE(remote_repo->IsRepoFake());
                            // Get subtree entry id from root tree id
                            auto entry_baz_t = remote_repo->GetSubtreeFromTree(
                                kRootId, "baz", logger);
                            REQUIRE(entry_baz_t);
                            CHECK(*entry_baz_t == kBazId);
                        } break;
                        case 2: {
                            auto remote_repo = GitRepo::Open(remote_cas);
                            REQUIRE(remote_repo);
                            REQUIRE(remote_repo->IsRepoFake());
                            // Find repository root from path
                            auto root_path_from_bazbar =
                                GitRepo::GetRepoRootFromPath(
                                    *remote_repo_path / "baz/bar", logger);
                            REQUIRE(root_path_from_bazbar);
                            CHECK(*root_path_from_bazbar == *remote_repo_path);
                        } break;
                        case 3: {
                            auto remote_repo = GitRepo::Open(remote_cas);
                            REQUIRE(remote_repo);
                            REQUIRE(remote_repo->IsRepoFake());
                            // Get subtree entry id from path
                            auto path_baz = *remote_repo_path / "baz";
                            auto entry_baz_p = remote_repo->GetSubtreeFromPath(
                                path_baz, kRootCommit, logger);
                            REQUIRE(entry_baz_p);
                            CHECK(*entry_baz_p == kBazId);
                        } break;
                        case 4: {
                            auto remote_repo = GitRepo::Open(remote_cas);
                            REQUIRE(remote_repo);
                            REQUIRE(remote_repo->IsRepoFake());
                            auto result_containing =
                                remote_repo->CheckCommitExists(kRootCommit,
                                                               logger);
                            CHECK(*result_containing);
                        } break;
                        // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
                        case 5: {
                            auto remote_repo = GitRepo::Open(remote_cas);
                            REQUIRE(remote_repo);
                            REQUIRE(remote_repo->IsRepoFake());
                            // set up tmp dir
                            // create tmp dir to use for fetch
                            auto tmp_path_fetch_branch =
                                TestUtils::GetRepoPath();
                            REQUIRE(FileSystemManager::CreateDirectory(
                                tmp_path_fetch_branch));
                            // fetch all
                            REQUIRE(remote_repo->LocalFetchViaTmpRepo(
                                tmp_path_fetch_branch,
                                *remote_repo_path,
                                std::nullopt,
                                logger));
                        } break;
                    }
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
