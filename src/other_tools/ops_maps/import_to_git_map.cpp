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

#include "src/other_tools/ops_maps/import_to_git_map.hpp"

#include "fmt/core.h"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/other_tools/just_mr/utils.hpp"

auto CreateImportToGitMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::size_t jobs) -> ImportToGitMap {
    auto import_to_git = [critical_git_op_map](auto ts,
                                               auto setter,
                                               auto logger,
                                               auto /*unused*/,
                                               auto const& key) {
        // Perform initial commit at location: init + add . + commit
        GitOpKey op_key = {
            {
                key.target_path,  // target_path
                "",               // git_hash
                "",               // branch
                fmt::format(
                    "Content of {} {}", key.repo_type, key.content),  // message
            },
            GitOpType::INITIAL_COMMIT};
        critical_git_op_map->ConsumeAfterKeysReady(
            ts,
            {std::move(op_key)},
            [critical_git_op_map,
             target_path = key.target_path,
             ts,
             setter,
             logger](auto const& values) {
                GitOpValue op_result = *values[0];
                // check flag
                if (not op_result.result) {
                    (*logger)("Initial commit failed",
                              /*fatal=*/true);
                    return;
                }
                // ensure Git cache
                // define Git operation to be done
                GitOpKey op_key = {
                    {
                        JustMR::Utils::GetGitCacheRoot(),  // target_path
                        "",                                // git_hash
                        "",                                // branch
                        std::nullopt,                      // message
                        true                               // init_bare
                    },
                    GitOpType::ENSURE_INIT};
                critical_git_op_map->ConsumeAfterKeysReady(
                    ts,
                    {std::move(op_key)},
                    [critical_git_op_map,
                     commit = *op_result.result,
                     target_path,
                     git_cas = op_result.git_cas,
                     ts,
                     setter,
                     logger](auto const& values) {
                        GitOpValue op_result = *values[0];
                        // check flag
                        if (not op_result.result) {
                            (*logger)("Git init failed",
                                      /*fatal=*/true);
                            return;
                        }
                        // fetch all into Git cache
                        auto just_git_repo = GitRepo::Open(op_result.git_cas);
                        if (not just_git_repo) {
                            (*logger)(fmt::format("Could not open Git "
                                                  "repository {}",
                                                  target_path.string()),
                                      /*fatal=*/true);
                            return;
                        }
                        // define temp repo path
                        auto tmp_repo_path = CreateUniquePath(target_path);
                        if (not tmp_repo_path) {
                            (*logger)(
                                fmt::format("Could not create unique path "
                                            "for target {}",
                                            target_path.string()),
                                /*fatal=*/true);
                            return;
                        }
                        auto wrapped_logger =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [&logger, target_path](auto const& msg,
                                                       bool fatal) {
                                    (*logger)(
                                        fmt::format("While fetch via tmp repo "
                                                    "for target {}:\n{}",
                                                    target_path.string(),
                                                    msg),
                                        fatal);
                                });
                        auto success =
                            just_git_repo->FetchViaTmpRepo(*tmp_repo_path,
                                                           target_path.string(),
                                                           "",
                                                           wrapped_logger);
                        if (not success) {
                            return;
                        }
                        // setup a wrapped_logger
                        wrapped_logger =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [&logger, target_path](auto const& msg,
                                                       bool fatal) {
                                    (*logger)(
                                        fmt::format("While doing keep commit "
                                                    "and setting Git tree:\n{}",
                                                    target_path.string(),
                                                    msg),
                                        fatal);
                                });
                        KeepCommitAndSetTree(critical_git_op_map,
                                             commit,
                                             target_path,
                                             git_cas,
                                             ts,
                                             setter,
                                             wrapped_logger);
                    },
                    [logger, target_path = JustMR::Utils::GetGitCacheRoot()](
                        auto const& msg, bool fatal) {
                        (*logger)(fmt::format("While running critical Git "
                                              "op ENSURE_INIT bare for "
                                              "target {}:\n{}",
                                              target_path.string(),
                                              msg),
                                  fatal);
                    });
            },
            [logger, target_path = key.target_path](auto const& msg,
                                                    bool fatal) {
                (*logger)(fmt::format("While running critical Git op "
                                      "INITIAL_COMMIT for target {}:\n{}",
                                      target_path.string(),
                                      msg),
                          fatal);
            });
    };
    return AsyncMapConsumer<CommitInfo, std::pair<std::string, GitCASPtr>>(
        import_to_git, jobs);
}

void KeepCommitAndSetTree(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::string const& commit,
    std::filesystem::path const& target_path,
    GitCASPtr const& git_cas,
    gsl::not_null<TaskSystem*> const& ts,
    ImportToGitMap::SetterPtr const& setter,
    ImportToGitMap::LoggerPtr const& logger) {
    // Keep tag for commit
    GitOpKey op_key = {{
                           JustMR::Utils::GetGitCacheRoot(),  // target_path
                           commit,                            // git_hash
                           "",                                // branch
                           "Keep referenced tree alive"       // message
                       },
                       GitOpType::KEEP_TAG};
    critical_git_op_map->ConsumeAfterKeysReady(
        ts,
        {std::move(op_key)},
        [commit, target_path, git_cas, setter, logger](auto const& values) {
            GitOpValue op_result = *values[0];
            // check flag
            if (not op_result.result) {
                (*logger)("Keep tag failed",
                          /*fatal=*/true);
                return;
            }
            auto git_repo = GitRepo::Open(git_cas);
            if (not git_repo) {
                (*logger)(fmt::format("Could not open Git repository {}",
                                      target_path.string()),
                          /*fatal=*/true);
                return;
            }
            // get tree id and return it
            auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [&logger, commit](auto const& msg, bool fatal) {
                    (*logger)(
                        fmt::format("While getting subtree from commit {}:\n{}",
                                    commit,
                                    msg),
                        fatal);
                });
            auto tree_hash =
                git_repo->GetSubtreeFromCommit(commit, ".", wrapped_logger);
            if (not tree_hash) {
                return;
            }
            (*setter)(std::pair<std::string, GitCASPtr>(*tree_hash, git_cas));
        },
        [logger, commit, target_path](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While running critical Git op KEEP_TAG for "
                                  "commit {} in target {}:\n{}",
                                  commit,
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}
