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
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/fs_utils.hpp"

namespace {

void KeepCommitAndSetTree(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::string const& commit,
    GitCASPtr const& just_git_cas,
    StorageConfig const& storage_config,
    gsl::not_null<TaskSystem*> const& ts,
    ImportToGitMap::SetterPtr const& setter,
    ImportToGitMap::LoggerPtr const& logger) {
    // Keep tag for commit
    GitOpKey op_key = {.params =
                           {
                               storage_config.GitRoot(),     // target_path
                               commit,                       // git_hash
                               "Keep referenced tree alive"  // message
                           },
                       .op_type = GitOpType::KEEP_TAG};
    critical_git_op_map->ConsumeAfterKeysReady(
        ts,
        {std::move(op_key)},
        [commit, just_git_cas, storage_config, setter, logger](
            auto const& values) {
            GitOpValue op_result = *values[0];
            // check flag
            if (not op_result.result) {
                (*logger)("Keep tag failed",
                          /*fatal=*/true);
                return;
            }
            auto just_git_repo = GitRepoRemote::Open(just_git_cas);
            if (not just_git_repo) {
                (*logger)(fmt::format("Could not open Git repository {}",
                                      storage_config.GitRoot().string()),
                          /*fatal=*/true);
                return;
            }
            // get tree id and return it
            auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [logger, commit](auto const& msg, bool fatal) {
                    (*logger)(
                        fmt::format("While getting subtree from commit {}:\n{}",
                                    commit,
                                    msg),
                        fatal);
                });
            auto res = just_git_repo->GetSubtreeFromCommit(
                commit, ".", wrapped_logger);
            if (not res) {
                return;
            }
            (*setter)(std::pair<std::string, GitCASPtr>(*std::move(res),
                                                        just_git_cas));
        },
        [logger, commit, target_path = storage_config.GitRoot()](
            auto const& msg, bool fatal) {
            (*logger)(fmt::format("While running critical Git op KEEP_TAG for "
                                  "commit {} in target {}:\n{}",
                                  commit,
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}

}  // namespace

auto CreateImportToGitMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    gsl::not_null<StorageConfig const*> const& storage_config,
    std::size_t jobs) -> ImportToGitMap {
    auto import_to_git = [critical_git_op_map,
                          git_bin,
                          launcher,
                          storage_config](auto ts,
                                          auto setter,
                                          auto logger,
                                          auto /*unused*/,
                                          auto const& key) {
        // The repository path that imports the content must be separate from
        // the content path, to avoid polluting the entries
        auto repo_dir = storage_config->CreateTypedTmpDir("import-repo");
        if (not repo_dir) {
            (*logger)(fmt::format("Failed to create import repository tmp "
                                  "directory for target {}",
                                  key.target_path.string()),
                      true);
            return;
        }
        // Commit content from target_path via the tmp repository
        GitOpKey op_key = {.params =
                               {
                                   repo_dir->GetPath(),  // target_path
                                   "",                   // git_hash
                                   fmt::format("Content of {} {}",
                                               key.repo_type,
                                               key.content),  // message
                                   key.target_path            // source_path
                               },
                           .op_type = GitOpType::INITIAL_COMMIT};
        critical_git_op_map->ConsumeAfterKeysReady(
            ts,
            {std::move(op_key)},
            [repo_dir,  // keep repo_dir alive
             critical_git_op_map,
             git_bin,
             launcher,
             storage_config,
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
                    .params =
                        {
                            storage_config->GitRoot(),  // target_path
                            "",                         // git_hash
                            std::nullopt,               // message
                            std::nullopt,               // source_path
                            true                        // init_bare
                        },
                    .op_type = GitOpType::ENSURE_INIT};
                critical_git_op_map->ConsumeAfterKeysReady(
                    ts,
                    {std::move(op_key)},
                    [repo_dir,  // keep repo_dir alive
                     critical_git_op_map,
                     commit = *op_result.result,
                     git_bin,
                     launcher,
                     storage_config,
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
                        auto just_git_repo =
                            GitRepoRemote::Open(op_result.git_cas);
                        if (not just_git_repo) {
                            (*logger)(
                                fmt::format("Could not open Git cache "
                                            "repository {}",
                                            storage_config->GitRoot().string()),
                                /*fatal=*/true);
                            return;
                        }
                        auto const& target_path = repo_dir->GetPath();
                        auto wrapped_logger =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [logger, target_path](auto const& msg,
                                                      bool fatal) {
                                    (*logger)(fmt::format("While fetch via tmp "
                                                          "repo from {}:\n{}",
                                                          target_path.string(),
                                                          msg),
                                              fatal);
                                });
                        if (not just_git_repo->FetchViaTmpRepo(
                                *storage_config,
                                target_path.string(),
                                std::nullopt,
                                std::vector<std::string>{} /* inherit_env */,
                                git_bin,
                                launcher,
                                wrapped_logger)) {
                            return;
                        }
                        // setup a wrapped_logger
                        wrapped_logger =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [logger, target_path](auto const& msg,
                                                      bool fatal) {
                                    (*logger)(
                                        fmt::format("While doing keep commit "
                                                    "and setting Git tree for "
                                                    "target {}:\n{}",
                                                    target_path.string(),
                                                    msg),
                                        fatal);
                                });
                        KeepCommitAndSetTree(critical_git_op_map,
                                             commit,
                                             op_result.git_cas, /*just_git_cas*/
                                             *storage_config,
                                             ts,
                                             setter,
                                             wrapped_logger);
                    },
                    [logger, target_path = storage_config->GitRoot()](
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
