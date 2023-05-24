// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/utils/cpp/atomic.hpp"
#include "test/utils/shell_quoting.hpp"

namespace {

auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo.bundle"};
auto const kRootCommit =
    std::string{"e4fc610c60716286b98cf51ad0c8f0d50f3aebb5"};
auto const kRootId = std::string{"c610db170fbcad5f2d66fe19972495923f3b2536"};

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

/*
NOTE: GitRepoRemote inherits from GitRepo all the methods relating to non-remote
Git operations. Those methods are already accounted for int he GitRepo tests,
therefore they are skipped here to avoid superfluous work.
*/

TEST_CASE("Open extended Git repo", "[git_repo_remote]") {
    SECTION("Fake bare repository") {
        auto repo_path = TestUtils::CreateTestRepo(true);
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepoRemote::Open(cas);
        REQUIRE(repo);
        CHECK(repo->GetGitCAS() == cas);  // same odb, same GitCAS
        CHECK(repo->IsRepoFake());
    }

    SECTION("Fake non-bare repository") {
        auto repo_path = TestUtils::CreateTestRepo();
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepoRemote::Open(cas);
        REQUIRE(repo);
        CHECK(repo->GetGitCAS() == cas);  // same odb, same GitCAS
        CHECK(repo->IsRepoFake());
    }

    SECTION("Real bare repository") {
        auto repo_path = TestUtils::CreateTestRepo(true);
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepoRemote::Open(*repo_path);
        REQUIRE(repo);
        CHECK_FALSE(repo->GetGitCAS() == cas);  // same odb, different GitCAS
        CHECK_FALSE(repo->IsRepoFake());
    }

    SECTION("Real non-bare repository") {
        auto repo_path = TestUtils::CreateTestRepo();
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepoRemote::Open(*repo_path);
        REQUIRE(repo);
        CHECK_FALSE(repo->GetGitCAS() == cas);  // same odb, different GitCAS
        CHECK_FALSE(repo->IsRepoFake());
    }

    SECTION("Non-existing repository") {
        auto repo = GitRepoRemote::Open("does_not_exist");
        REQUIRE(repo == std::nullopt);
    }

    SECTION("Initialize and open bare repository") {
        auto repo_path = TestUtils::GetRepoPath();
        auto repo = GitRepoRemote::InitAndOpen(repo_path, /*is_bare=*/true);
        REQUIRE(repo);
        CHECK_FALSE(repo->IsRepoFake());
    }

    SECTION("Real non-bare repository with checkout") {
        auto repo_path = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(repo_path);
        auto cas = GitCAS::Open(*repo_path);
        REQUIRE(cas);

        auto repo = GitRepoRemote::Open(cas);
        REQUIRE(repo);
        CHECK(repo->GetGitCAS() == cas);
        CHECK(repo->IsRepoFake());
    }
}

TEST_CASE("Single-threaded real repository remote operations",
          "[git_repo_remote]") {
    auto repo_path = TestUtils::CreateTestRepoWithCheckout();
    REQUIRE(repo_path);

    // setup dummy logger
    auto logger = std::make_shared<GitRepoRemote::anon_logger_t>(
        [](auto const& msg, bool fatal) {
            Logger::Log(fatal ? LogLevel::Error : LogLevel::Progress,
                        std::string(msg));
        });

    SECTION("Get commit id from remote") {
        // make bare real repo to call remote ls from
        auto path_remote_ls_bare = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(path_remote_ls_bare);
        auto repo_remote_ls_bare = GitRepoRemote::Open(*path_remote_ls_bare);
        REQUIRE(repo_remote_ls_bare);

        // remote ls
        auto remote_commit = repo_remote_ls_bare->GetCommitFromRemote(
            nullptr, *repo_path, "master", logger);
        REQUIRE(remote_commit);
        CHECK(*remote_commit == kRootCommit);
    }

    SECTION("Fetch with base refspecs from remote") {
        // make bare real repo to fetch into
        auto path_fetch_all_bare = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(path_fetch_all_bare);
        auto repo_fetch_all_bare = GitRepoRemote::Open(*path_fetch_all_bare);

        // fetch
        CHECK(repo_fetch_all_bare->FetchFromRemote(
            nullptr, *repo_path, std::nullopt, logger));
    }

    SECTION("Fetch branch from remote") {
        // make bare real repo to fetch into
        auto path_fetch_branch_bare = TestUtils::CreateTestRepoWithCheckout();
        REQUIRE(path_fetch_branch_bare);
        auto repo_fetch_branch_bare =
            GitRepoRemote::Open(*path_fetch_branch_bare);
        REQUIRE(repo_fetch_branch_bare);

        // fetch
        CHECK(repo_fetch_branch_bare->FetchFromRemote(
            nullptr, *repo_path, "master", logger));
    }
}

TEST_CASE("Single-threaded fake repository operations", "[git_repo_remote]") {
    auto repo_path = TestUtils::CreateTestRepoWithCheckout();
    REQUIRE(repo_path);
    auto cas = GitCAS::Open(*repo_path);
    REQUIRE(cas);
    auto repo = GitRepoRemote::Open(cas);
    REQUIRE(repo);
    REQUIRE(repo->GetGitCAS() == cas);
    REQUIRE(repo->IsRepoFake());

    // setup dummy logger
    auto logger = std::make_shared<GitRepoRemote::anon_logger_t>(
        [](auto const& msg, bool fatal) {
            Logger::Log(fatal ? LogLevel::Error : LogLevel::Progress,
                        std::string(msg));
        });

    SECTION("Fetch objects from remote via temporary repository") {
        SECTION("Fetch all into repository") {
            // set repo to fetch into
            auto path_fetch_all = TestUtils::GetRepoPath();
            auto repo_fetch_all =
                GitRepoRemote::InitAndOpen(path_fetch_all, /*is_bare=*/true);
            REQUIRE(repo_fetch_all);

            // check commit is not there before fetch
            CHECK_FALSE(
                *repo_fetch_all->CheckCommitExists(kRootCommit, logger));

            // create tmp dir to use for fetch
            auto tmp_path_fetch_all = TestUtils::GetRepoPath();
            REQUIRE(FileSystemManager::CreateDirectory(tmp_path_fetch_all));
            // fetch all with base refspecs
            REQUIRE(repo_fetch_all->FetchViaTmpRepo(tmp_path_fetch_all,
                                                    *repo_path,
                                                    std::nullopt,
                                                    "git",
                                                    {},
                                                    logger));

            // check commit is there after fetch
            CHECK(*repo_fetch_all->CheckCommitExists(kRootCommit, logger));
        }

        SECTION("Fetch with refspec into repository") {
            // set repo to fetch into
            auto path_fetch_wRefspec = TestUtils::GetRepoPath();
            auto repo_fetch_wRefspec = GitRepoRemote::InitAndOpen(
                path_fetch_wRefspec, /*is_bare=*/true);
            REQUIRE(repo_fetch_wRefspec);

            // check commit is not there before fetch
            CHECK_FALSE(
                *repo_fetch_wRefspec->CheckCommitExists(kRootCommit, logger));

            // create tmp dir to use for fetch
            auto tmp_path_fetch_wRefspec = TestUtils::GetRepoPath();
            REQUIRE(
                FileSystemManager::CreateDirectory(tmp_path_fetch_wRefspec));
            // fetch all
            REQUIRE(
                repo_fetch_wRefspec->FetchViaTmpRepo(tmp_path_fetch_wRefspec,
                                                     *repo_path,
                                                     "master",
                                                     "git",
                                                     {},
                                                     logger));

            // check commit is there after fetch
            CHECK(*repo_fetch_wRefspec->CheckCommitExists(kRootCommit, logger));
        }
    }

    SECTION("Update commit from remote via temporary repository") {
        auto path_commit_upd = TestUtils::GetRepoPath();
        auto repo_commit_upd =
            GitRepoRemote::InitAndOpen(path_commit_upd, /*is_bare=*/true);
        REQUIRE(repo_commit_upd);

        // create tmp dir to use for commits update
        auto tmp_path_commit_upd = TestUtils::GetRepoPath();
        REQUIRE(FileSystemManager::CreateDirectory(tmp_path_commit_upd));
        // do remote ls
        auto fetched_commit = repo_commit_upd->UpdateCommitViaTmpRepo(
            tmp_path_commit_upd, *repo_path, "master", "git", {}, logger);

        REQUIRE(fetched_commit);
        CHECK(*fetched_commit == kRootCommit);
    }
}

TEST_CASE("Multi-threaded fake repository operations", "[git_repo_remote]") {
    /*
    Test all fake repository operations while being done in parallel.
    They are supposed to be thread-safe, so no conflicts should exist.
    */
    // define remote, for ops that need it
    auto remote_repo_path = TestUtils::CreateTestRepoWithCheckout();
    REQUIRE(remote_repo_path);
    auto remote_cas = GitCAS::Open(*remote_repo_path);
    REQUIRE(remote_cas);
    auto remote_repo = GitRepoRemote::Open(remote_cas);
    REQUIRE(remote_repo);
    REQUIRE(remote_repo->GetGitCAS() == remote_cas);
    REQUIRE(remote_repo->IsRepoFake());

    // setup dummy logger
    auto logger = std::make_shared<GitRepoRemote::anon_logger_t>(
        [](auto const& msg, bool fatal) {
            Logger::Log(fatal ? LogLevel::Error : LogLevel::Progress,
                        std::string(msg));
        });

    // setup threading
    constexpr auto kNumThreads = 100;

    atomic<bool> starting_signal{false};
    std::vector<std::thread> threads{};
    threads.reserve(kNumThreads);

    // define target repo, from which fetch ops will be initiated
    auto target_repo_path = TestUtils::GetRepoPath();
    auto target_repo =
        GitRepoRemote::InitAndOpen(target_repo_path, /*is_bare=*/true);
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
                            // create tmp dir to use for fetch
                            auto tmp_path_fetch_all = TestUtils::GetRepoPath();
                            REQUIRE(FileSystemManager::CreateDirectory(
                                tmp_path_fetch_all));
                            // fetch with base refspecs
                            CHECK(
                                target_repo->FetchViaTmpRepo(tmp_path_fetch_all,
                                                             *remote_repo_path,
                                                             std::nullopt,
                                                             "git",
                                                             {},
                                                             logger));
                        } break;
                        case 2: {
                            // create tmp dir to use for fetch
                            auto tmp_path_fetch_wRefspec =
                                TestUtils::GetRepoPath();
                            REQUIRE(FileSystemManager::CreateDirectory(
                                tmp_path_fetch_wRefspec));
                            // fetch specific branch
                            CHECK(target_repo->FetchViaTmpRepo(
                                tmp_path_fetch_wRefspec,
                                *remote_repo_path,
                                "master",
                                "git",
                                {},
                                logger));
                        } break;
                        case 3: {
                            // create tmp dir to use for commits update
                            auto tmp_path_commit_upd = TestUtils::GetRepoPath();
                            REQUIRE(FileSystemManager::CreateDirectory(
                                tmp_path_commit_upd));
                            // do remote ls
                            auto fetched_commit =
                                target_repo->UpdateCommitViaTmpRepo(
                                    tmp_path_commit_upd,
                                    *remote_repo_path,
                                    "master",
                                    "git",
                                    {},
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
