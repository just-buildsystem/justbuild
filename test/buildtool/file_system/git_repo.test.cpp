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

#include "catch2/catch.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/utils/cpp/atomic.hpp"

namespace {

auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo.bundle"};
auto const kRootCommit =
    std::string{"bc5f88b46bbf0c4c61da7a1296fa9a0559b92822"};
auto const kRootId = std::string{"e51a219a27b672ccf17abec7d61eb4d6e0424140"};
auto const kFooId = std::string{"19102815663d23f8b75a47e7a01965dcdc96468c"};
auto const kBarId = std::string{"ba0e162e1c47469e3fe4b393a8bf8c569f302116"};
auto const kBazOneId = std::string{"c610db170fbcad5f2d66fe19972495923f3b2536"};
auto const kBazTwoId = std::string{"27b32561185c2825150893774953906c6daa6798"};

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
        auto cmd = fmt::format(
            "git --git-dir={} --work-tree={} checkout master",
            is_bare ? repo_path->string() : (*repo_path / ".git").string(),
            repo_path->string());
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
                               kBundlePath,
                               repo_path.string());
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
}

TEST_CASE("Single-threaded real repository remote operations", "[git_repo]") {
    auto repo_path = TestUtils::CreateTestRepoWithCheckout();
    REQUIRE(repo_path);

    // setup dummy logger
    auto logger = std::make_shared<GitRepo::anon_logger_t>(
        [](auto const& msg, bool fatal) {
            Logger::Log(fatal ? LogLevel::Error : LogLevel::Progress,
                        std::string(msg));
        });

    SECTION("Get commit id from remote") {
        // make bare real repo to call remote ls from
        auto path_remote_ls_bare = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(path_remote_ls_bare);
        auto repo_remote_ls_bare = GitRepo::Open(*path_remote_ls_bare);
        REQUIRE(repo_remote_ls_bare);

        // get refname of branch
        auto branch_refname =
            repo_remote_ls_bare->GetBranchLocalRefname("master", logger);
        REQUIRE(branch_refname);
        REQUIRE(*branch_refname == "refs/heads/master");
        // remote ls
        auto remote_commit = repo_remote_ls_bare->GetCommitFromRemote(
            *repo_path, *branch_refname, logger);
        REQUIRE(remote_commit);
        CHECK(*remote_commit == kRootCommit);
    }

    SECTION("Fetch all from remote") {
        // make bare real repo to fetch into
        auto path_fetch_all_bare = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(path_fetch_all_bare);
        auto repo_fetch_all_bare = GitRepo::Open(*path_fetch_all_bare);

        // fetch
        CHECK(repo_fetch_all_bare->FetchFromRemote(*repo_path, "", logger));
    }

    SECTION("Fetch branch from remote") {
        // make bare real repo to fetch into
        auto path_fetch_branch_bare = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(path_fetch_branch_bare);
        auto repo_fetch_branch_bare = GitRepo::Open(*path_fetch_branch_bare);
        REQUIRE(repo_fetch_branch_bare);

        // get refspec of branch
        auto branch_refname =
            repo_fetch_branch_bare->GetBranchLocalRefname("master", logger);
        REQUIRE(branch_refname);
        auto branch_refspec = std::string("+") + *branch_refname;
        REQUIRE(branch_refspec == "+refs/heads/master");
        // fetch
        CHECK(repo_fetch_branch_bare->FetchFromRemote(
            *repo_path, branch_refspec, logger));
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
            REQUIRE(entry_root_c);
            CHECK(*entry_root_c == kRootId);
        }

        SECTION("Get inner tree id") {
            auto entry_baz_c =
                repo->GetSubtreeFromCommit(kRootCommit, "baz", logger);
            REQUIRE(entry_baz_c);
            CHECK(*entry_baz_c == kBazOneId);
        }

        SECTION("Get inner blob id") {
            auto entry_bazbazfoo_c =
                repo->GetSubtreeFromCommit(kRootCommit, "baz/baz/foo", logger);
            REQUIRE(entry_bazbazfoo_c);
            CHECK(*entry_bazbazfoo_c == kFooId);
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
            CHECK(*entry_baz_t == kBazOneId);
        }
        SECTION("Get inner blob id") {
            auto entry_bazbazfoo_t =
                repo->GetSubtreeFromTree(kRootId, "baz/baz/foo", logger);
            REQUIRE(entry_bazbazfoo_t);
            CHECK(*entry_bazbazfoo_t == kFooId);
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

            auto root_path_from_bazbazfoo = GitRepo::GetRepoRootFromPath(
                *repo_path / "baz/baz/foo", logger);
            REQUIRE(root_path_from_bazbazfoo);
            CHECK(*root_path_from_bazbazfoo == *repo_path);

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
            CHECK(*entry_baz_p == kBazOneId);
        }

        SECTION("Get inner blob id") {
            auto path_bazbazfoo = *repo_path / "baz/baz/foo";
            auto entry_bazbazfoo_p =
                repo->GetSubtreeFromPath(path_bazbazfoo, kRootCommit, logger);
            REQUIRE(entry_bazbazfoo_p);
            CHECK(*entry_bazbazfoo_p == kFooId);
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

    SECTION("Fetch objects from remote via temporary repository") {
        SECTION("Fetch all into repository") {
            // set repo to fetch into
            auto path_fetch_all = TestUtils::GetRepoPath();
            auto repo_fetch_all =
                GitRepo::InitAndOpen(path_fetch_all, /*is_bare=*/true);
            REQUIRE(repo_fetch_all);

            // check commit is not there before fetch
            CHECK_FALSE(
                *repo_fetch_all->CheckCommitExists(kRootCommit, logger));

            // create path for tmp repo to use for fetch
            auto tmp_path_fetch_all = TestUtils::GetRepoPath();
            // fetch all
            REQUIRE(repo_fetch_all->FetchViaTmpRepo(
                tmp_path_fetch_all, *repo_path, "", logger));

            // check commit is there after fetch
            CHECK(*repo_fetch_all->CheckCommitExists(kRootCommit, logger));
        }

        SECTION("Fetch with refspec into repository") {
            // set repo to fetch into
            auto path_fetch_wRefspec = TestUtils::GetRepoPath();
            auto repo_fetch_wRefspec =
                GitRepo::InitAndOpen(path_fetch_wRefspec, /*is_bare=*/true);
            REQUIRE(repo_fetch_wRefspec);

            // check commit is not there before fetch
            CHECK_FALSE(
                *repo_fetch_wRefspec->CheckCommitExists(kRootCommit, logger));

            // create path for tmp repo to use for fetch
            auto tmp_path_fetch_wRefspec = TestUtils::GetRepoPath();
            // fetch all
            REQUIRE(
                repo_fetch_wRefspec->FetchViaTmpRepo(tmp_path_fetch_wRefspec,
                                                     *repo_path,
                                                     "+refs/heads/master",
                                                     logger));

            // check commit is there after fetch
            CHECK(*repo_fetch_wRefspec->CheckCommitExists(kRootCommit, logger));
        }
    }

    SECTION("Update commit from remote via temporary repository") {
        auto path_commit_upd = TestUtils::GetRepoPath();
        auto repo_commit_upd =
            GitRepo::InitAndOpen(path_commit_upd, /*is_bare=*/true);
        REQUIRE(repo_commit_upd);

        // create path for tmp repo to use for remote ls
        auto tmp_path_commit_upd = TestUtils::GetRepoPath();
        // do remote ls
        auto fetched_commit = repo_commit_upd->UpdateCommitViaTmpRepo(
            tmp_path_commit_upd, *repo_path, "refs/heads/master", logger);

        REQUIRE(fetched_commit);
        CHECK(*fetched_commit == kRootCommit);
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
    auto remote_repo = GitRepo::Open(remote_cas);
    REQUIRE(remote_repo);
    REQUIRE(remote_repo->GetGitCAS() == remote_cas);
    REQUIRE(remote_repo->IsRepoFake());

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
        constexpr int NUM_CASES = 5;
        for (int id{}; id < kNumThreads; ++id) {
            threads.emplace_back(
                [&remote_repo, &remote_repo_path, &logger, &starting_signal](
                    int tid) {
                    starting_signal.wait(false);
                    // cases based on thread number
                    switch (tid % NUM_CASES) {
                        case 0: {
                            // Get subtree entry id from commit
                            auto entry_bazbar_c =
                                remote_repo->GetSubtreeFromCommit(
                                    kRootCommit, "baz/bar", logger);
                            REQUIRE(entry_bazbar_c);
                            CHECK(*entry_bazbar_c == kBarId);
                        } break;
                        case 1: {
                            // Get subtree entry id from root tree id
                            auto entry_bazbar_t =
                                remote_repo->GetSubtreeFromTree(
                                    kRootId, "baz/bar", logger);
                            REQUIRE(entry_bazbar_t);
                            CHECK(*entry_bazbar_t == kBarId);
                        } break;
                        case 2: {
                            // Find repository root from path
                            auto root_path_from_bazbar =
                                GitRepo::GetRepoRootFromPath(
                                    *remote_repo_path / "baz/bar", logger);
                            REQUIRE(root_path_from_bazbar);
                            CHECK(*root_path_from_bazbar == *remote_repo_path);
                        } break;
                        case 3: {
                            // Get subtree entry id from path
                            auto path_bazbar = *remote_repo_path / "baz/bar";
                            auto entry_bazbar_p =
                                remote_repo->GetSubtreeFromPath(
                                    path_bazbar, kRootCommit, logger);
                            REQUIRE(entry_bazbar_p);
                            CHECK(*entry_bazbar_p == kBarId);
                        } break;
                        case 4: {
                            auto result_containing =
                                remote_repo->CheckCommitExists(kRootCommit,
                                                               logger);
                            CHECK(*result_containing);
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

    // define target repo, from which fetch ops will be initiated
    auto target_repo_path = TestUtils::GetRepoPath();
    auto target_repo = GitRepo::InitAndOpen(target_repo_path, /*is_bare=*/true);
    REQUIRE(target_repo);

    SECTION("Fetching into same repository from remote") {
        constexpr int NUM_CASES = 4;
        for (int id{}; id < kNumThreads; ++id) {
            threads.emplace_back(
                [&target_repo, &remote_repo_path, &logger, &starting_signal](
                    int tid) {
                    starting_signal.wait(false);
                    // cases based on thread number
                    switch (tid % NUM_CASES) {
                        case 0: {
                            auto result_containing =
                                target_repo->CheckCommitExists(kRootCommit,
                                                               logger);
                            CHECK(result_containing);  // check it returns
                                                       // something
                        } break;
                        case 1: {
                            // create path for tmp repo to use for fetch
                            auto tmp_path_fetch_all = TestUtils::GetRepoPath();
                            CHECK(
                                target_repo->FetchViaTmpRepo(tmp_path_fetch_all,
                                                             *remote_repo_path,
                                                             "",
                                                             logger));
                        } break;
                        case 2: {
                            // create path for tmp repo to use for fetch
                            auto tmp_path_fetch_wRefspec =
                                TestUtils::GetRepoPath();
                            // fetch all
                            CHECK(target_repo->FetchViaTmpRepo(
                                tmp_path_fetch_wRefspec,
                                *remote_repo_path,
                                "+refs/heads/master",
                                logger));
                        } break;
                        case 3: {
                            // create path for tmp repo to use for remote ls
                            auto tmp_path_commit_upd = TestUtils::GetRepoPath();
                            // do remote ls
                            auto fetched_commit =
                                target_repo->UpdateCommitViaTmpRepo(
                                    tmp_path_commit_upd,
                                    *remote_repo_path,
                                    "refs/heads/master",
                                    logger);

                            REQUIRE(fetched_commit);
                            CHECK(*fetched_commit == kRootCommit);
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
