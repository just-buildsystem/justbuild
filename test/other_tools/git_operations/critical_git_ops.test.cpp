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
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "test/utils/shell_quoting.hpp"

namespace {

auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo.bundle"};
auto const kRootCommit =
    std::string{"bc5f88b46bbf0c4c61da7a1296fa9a0559b92822"};

}  // namespace

/// \brief TestUtils that accounts for multi-process calls.
/// Ensures the git clone only happens once per path.
/// Can also create process-unique paths.
class TestUtilsMP {
  public:
    [[nodiscard]] static auto GetUniqueTestDir() noexcept
        -> std::optional<std::filesystem::path> {
        auto* tmp_dir = std::getenv("TEST_TMPDIR");
        if (tmp_dir != nullptr) {
            return CreateUniquePath(tmp_dir);
        }
        return CreateUniquePath(FileSystemManager::GetCurrentDirectory() /
                                "test/other_tools");
    }

    [[nodiscard]] static auto GetRepoPath(
        std::filesystem::path const& prefix) noexcept -> std::filesystem::path {
        return prefix / "test_git_repo" /
               std::filesystem::path{std::to_string(counter++)}.filename();
    }

    [[nodiscard]] static auto GetRepoPathUnique(
        std::filesystem::path const& prefix) noexcept -> std::filesystem::path {
        return prefix / ("test_git_repo." + CreateProcessUniqueId().value()) /
               std::filesystem::path{std::to_string(counter++)}.filename();
    }

    // The checkout will make the content available, as well as the HEAD ref
    [[nodiscard]] static auto CreateTestRepoWithCheckout(
        std::filesystem::path const& prefix,
        bool is_bare = false) noexcept -> std::optional<std::filesystem::path> {
        auto repo_path = CreateTestRepo(prefix, is_bare);
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

    [[nodiscard]] static auto CreateTestRepo(
        std::filesystem::path const& prefix,
        bool is_bare = false) noexcept -> std::optional<std::filesystem::path> {
        auto repo_path = GetRepoPath(prefix);
        std::optional<std::filesystem::path> result = std::nullopt;
        // only do work if another process hasn't already been here
        if (not FileSystemManager::Exists(repo_path)) {
            auto cmd = fmt::format("git clone {}{} {}",
                                   is_bare ? "--bare " : "",
                                   QuoteForShell(kBundlePath),
                                   QuoteForShell(repo_path.string()));
            if (std::system(cmd.c_str()) == 0) {
                result = repo_path;
            }
        }
        else {
            result = repo_path;
        }
        return result;
    }

  private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static inline std::atomic<int> counter = 0;
};

TEST_CASE("Crtitical git operations", "[critical_git_op_map]") {
    // setup the repos needed
    auto prefix = TestUtilsMP::GetUniqueTestDir();
    REQUIRE(prefix);

    auto testdir = *prefix / "test_git_repo";
    REQUIRE(FileSystemManager::CreateDirectory(testdir));

    // create the remote for the fetch ops
    auto remote_repo_path = TestUtilsMP::CreateTestRepoWithCheckout(*prefix);
    REQUIRE(remote_repo_path);

    // create the target paths for the various critical ops
    // IMPORTANT! For non-init critical ops the paths need to exist already!
    // 1. Initial commit -> needs a path containing some files
    // This has to be process unique, as the commit will fail otherwise!
    auto path_init_commit = TestUtilsMP::GetRepoPathUnique(*prefix);
    REQUIRE(FileSystemManager::WriteFile(
        "test no 1", path_init_commit / "test1.txt", true));
    REQUIRE(FileSystemManager::WriteFile(
        "test no 2", path_init_commit / "test2.txt", true));
    // 2 & 3. Initializing repos -> need only the paths
    auto path_init_bare = TestUtilsMP::GetRepoPath(*prefix);
    auto path_init_non_bare = TestUtilsMP::GetRepoPath(*prefix);
    // 4. Tag a commit -> needs a repo with a commit
    auto path_keep_tag = TestUtilsMP::CreateTestRepo(*prefix, true);
    REQUIRE(path_keep_tag);
    // 5. Get head commit -> needs a repo with a commit
    auto path_get_head_id = TestUtilsMP::CreateTestRepoWithCheckout(*prefix);
    REQUIRE(path_get_head_id);

    // create the map
    auto crit_op_guard = std::make_shared<CriticalGitOpGuard>();
    auto crit_op_map = CreateCriticalGitOpMap(crit_op_guard);

    // Add ops to the map. None should throw, as repeating the same operation
    // should retrieve the value from the map, not call the operation again.
    // helper lists
    const std::vector<size_t> ops_all{
        0, 1, 2, 3, 4};  // indices of all ops tested
    const std::vector<size_t> ops_with_result{
        0, 4};  // indices of ops that return a non-empty string
    // Add to the map all ops multiple times
    for ([[maybe_unused]] auto const& i :
         {0, 0, 0}) {  // replace once ranges are available
                       // (https://en.cppreference.com/w/cpp/ranges/iota_view)
        auto error = false;
        auto error_msg = std::string("NONE");
        {
            TaskSystem ts;
            crit_op_map.ConsumeAfterKeysReady(
                &ts,
                {GitOpKey{GitOpParams{
                              path_init_commit,  // target_path
                              "",                // git_hash
                              "",                // branch
                              "Init commit"      // message
                          },
                          GitOpType::INITIAL_COMMIT},
                 GitOpKey{GitOpParams{
                              path_init_bare,  // target_path
                              "",              // git_hash
                              "",              // branch
                              std::nullopt,    // message
                              true             // init_bare
                          },
                          GitOpType::ENSURE_INIT},
                 GitOpKey{GitOpParams{
                              path_init_non_bare,  // target_path
                              "",                  // git_hash
                              "",                  // branch
                              std::nullopt,        // message
                              false                // init_bare
                          },
                          GitOpType::ENSURE_INIT},
                 GitOpKey{GitOpParams{
                              *path_keep_tag,  // target_path
                              kRootCommit,     // git_hash
                              "",              // branch
                              "keep-me"        // message
                          },
                          GitOpType::KEEP_TAG},
                 GitOpKey{GitOpParams{
                              *path_get_head_id,  // target_path
                              "",                 // git_hash
                              "",                 // branch
                          },
                          GitOpType::GET_HEAD_ID}},
                [&ops_all, &ops_with_result](auto const& values) {
                    // check operations
                    for (size_t const& i : ops_all) {
                        auto res = *values[i];
                        REQUIRE(res.git_cas);
                        REQUIRE(res.result);
                        if (std::find(ops_with_result.begin(),
                                      ops_with_result.end(),
                                      i) != ops_with_result.end()) {
                            CHECK(not res.result->empty());
                        }
                    }
                },
                [&error, &error_msg](std::string const& msg, bool /*unused*/) {
                    error = true;
                    error_msg = msg;
                });
        }
        CHECK_FALSE(error);
        CHECK(error_msg == "NONE");
    }
}
