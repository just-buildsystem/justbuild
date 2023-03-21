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

#include "src/other_tools/root_maps/tree_id_git_map.hpp"

#include "fmt/core.h"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/system/system_command.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/just_mr/utils.hpp"

namespace {

void KeepCommitAndSetRoot(
    TreeIdInfo const& tree_id_info,
    std::string const& commit,
    TmpDirPtr const& tmp_dir,  // need to keep tmp_dir alive
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<TaskSystem*> const& ts,
    TreeIdGitMap::SetterPtr const& ws_setter,
    TreeIdGitMap::LoggerPtr const& logger) {
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
        [tmp_dir,  // keep tmp_dir alive
         tree_id_info,
         ws_setter,
         logger](auto const& values) {
            GitOpValue op_result = *values[0];
            // check flag
            if (not op_result.result) {
                (*logger)("Keep tag failed",
                          /*fatal=*/true);
                return;
            }
            // set the workspace root
            JustMRProgress::Instance().TaskTracker().Start(tree_id_info.origin);
            (*ws_setter)(
                std::pair(nlohmann::json::array(
                              {"git tree",
                               tree_id_info.hash,
                               JustMR::Utils::GetGitCacheRoot().string()}),
                          false));
        },
        [logger, commit, target_path = tmp_dir->GetPath()](auto const& msg,
                                                           bool fatal) {
            (*logger)(fmt::format("While running critical Git op KEEP_TAG for "
                                  "commit {} in target {}:\n{}",
                                  commit,
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}

}  // namespace

auto CreateTreeIdGitMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::vector<std::string> const& launcher,
    std::size_t jobs) -> TreeIdGitMap {
    auto tree_to_git = [critical_git_op_map, launcher](auto ts,
                                                       auto setter,
                                                       auto logger,
                                                       auto /*unused*/,
                                                       auto const& key) {
        // first, check whether tree exists already in CAS
        // ensure Git cache
        // define Git operation to be done
        GitOpKey op_key = {{
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
            [critical_git_op_map, launcher, key, ts, setter, logger](
                auto const& values) {
                GitOpValue op_result = *values[0];
                // check flag
                if (not op_result.result) {
                    (*logger)("Git cache init failed",
                              /*fatal=*/true);
                    return;
                }
                // Open fake tmp repo to check if tree is known to Git cache
                auto git_repo = GitRepoRemote::Open(
                    op_result.git_cas);  // link fake repo to odb
                if (not git_repo) {
                    (*logger)(
                        fmt::format("Could not open repository {}",
                                    JustMR::Utils::GetGitCacheRoot().string()),
                        /*fatal=*/true);
                    return;
                }
                // setup wrapped logger
                auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                    [logger](auto const& msg, bool fatal) {
                        (*logger)(fmt::format("While checking tree exists in "
                                              "Git cache:\n{}",
                                              msg),
                                  fatal);
                    });
                // check if the desired tree ID is in Git cache
                auto tree_found =
                    git_repo->CheckTreeExists(key.hash, wrapped_logger);
                if (not tree_found) {
                    // errors encountered
                    return;
                }
                if (not *tree_found) {
                    JustMRProgress::Instance().TaskTracker().Start(key.origin);
                    // create temporary location for command execution root
                    auto tmp_dir = JustMR::Utils::CreateTypedTmpDir("git-tree");
                    if (not tmp_dir) {
                        (*logger)(
                            "Failed to create tmp directory for tree id map!",
                            /*fatal=*/true);
                        return;
                    }
                    // create temporary location for storing command result
                    // files
                    auto out_dir = JustMR::Utils::CreateTypedTmpDir("git-tree");
                    if (not out_dir) {
                        (*logger)(
                            "Failed to create tmp directory for tree id map!",
                            /*fatal=*/true);
                        return;
                    }
                    // execute command in temporary location
                    SystemCommand system{key.hash};
                    auto cmdline = launcher;
                    std::copy(key.command.begin(),
                              key.command.end(),
                              std::back_inserter(cmdline));
                    auto const command_output =
                        system.Execute(cmdline,
                                       key.env_vars,
                                       tmp_dir->GetPath(),
                                       out_dir->GetPath());
                    if (not command_output) {
                        (*logger)(fmt::format("Failed to execute command:\n{}",
                                              nlohmann::json(cmdline).dump()),
                                  /*fatal=*/true);
                        return;
                    }

                    // do an import to git with tree check
                    GitOpKey op_key = {{
                                           tmp_dir->GetPath(),  // target_path
                                           "",                  // git_hash
                                           "",                  // branch
                                           fmt::format("Content of tree {}",
                                                       key.hash),  // message
                                       },
                                       GitOpType::INITIAL_COMMIT};
                    critical_git_op_map->ConsumeAfterKeysReady(
                        ts,
                        {std::move(op_key)},
                        [tmp_dir,  // keep tmp_dir alive
                         out_dir,  // keep stdout/stderr of command alive
                         critical_git_op_map,
                         just_git_cas = op_result.git_cas,
                         cmdline,
                         command_output,
                         key,
                         launcher,
                         ts,
                         setter,
                         logger](auto const& values) {
                            GitOpValue op_result = *values[0];
                            // check flag
                            if (not op_result.result) {
                                (*logger)("Commit failed",
                                          /*fatal=*/true);
                                return;
                            }
                            // Open fake tmp repository to check for tree
                            auto git_repo = GitRepoRemote::Open(
                                op_result.git_cas);  // link fake repo to odb
                            if (not git_repo) {
                                (*logger)(
                                    fmt::format("Could not open repository {}",
                                                tmp_dir->GetPath().string()),
                                    /*fatal=*/true);
                                return;
                            }
                            // setup wrapped logger
                            auto wrapped_logger =
                                std::make_shared<AsyncMapConsumerLogger>(
                                    [logger](auto const& msg, bool fatal) {
                                        (*logger)(
                                            fmt::format("While checking tree "
                                                        "exists:\n{}",
                                                        msg),
                                            fatal);
                                    });
                            // check that the desired tree ID is part of the
                            // repo
                            auto tree_check = git_repo->CheckTreeExists(
                                key.hash, wrapped_logger);
                            if (not tree_check) {
                                // errors encountered
                                return;
                            }
                            if (not *tree_check) {
                                std::string out_str{};
                                std::string err_str{};
                                auto cmd_out = FileSystemManager::ReadFile(
                                    command_output->stdout_file);
                                auto cmd_err = FileSystemManager::ReadFile(
                                    command_output->stderr_file);
                                if (cmd_out) {
                                    out_str = *cmd_out;
                                }
                                if (cmd_err) {
                                    err_str = *cmd_err;
                                }
                                std::string output{};
                                if (!out_str.empty() || !err_str.empty()) {
                                    output = fmt::format(
                                        ".\nOutput of command:\n{}{}",
                                        out_str,
                                        err_str);
                                }
                                (*logger)(
                                    fmt::format("Executing {} did not create "
                                                "specified tree {}{}",
                                                nlohmann::json(cmdline).dump(),
                                                key.hash,
                                                output),
                                    /*fatal=*/true);
                                return;
                            }
                            auto target_path = tmp_dir->GetPath();
                            // fetch all into Git cache
                            auto just_git_repo =
                                GitRepoRemote::Open(just_git_cas);
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
                                (*logger)(fmt::format("Could not create unique "
                                                      "path for target {}",
                                                      target_path.string()),
                                          /*fatal=*/true);
                                return;
                            }
                            wrapped_logger =
                                std::make_shared<AsyncMapConsumerLogger>(
                                    [logger, target_path](auto const& msg,
                                                          bool fatal) {
                                        (*logger)(
                                            fmt::format(
                                                "While fetch via tmp repo "
                                                "for target {}:\n{}",
                                                target_path.string(),
                                                msg),
                                            fatal);
                                    });
                            auto success = just_git_repo->FetchViaTmpRepo(
                                *tmp_repo_path,
                                target_path.string(),
                                std::nullopt,
                                launcher,
                                wrapped_logger);
                            if (not success) {
                                return;
                            }
                            // setup a wrapped_logger
                            wrapped_logger =
                                std::make_shared<AsyncMapConsumerLogger>(
                                    [logger, target_path](auto const& msg,
                                                          bool fatal) {
                                        (*logger)(fmt::format(
                                                      "While doing keep commit "
                                                      "and setting Git tree "
                                                      "for target {}:\n{}",
                                                      target_path.string(),
                                                      msg),
                                                  fatal);
                                    });
                            KeepCommitAndSetRoot(
                                key,
                                *op_result.result,  // commit id
                                tmp_dir,
                                critical_git_op_map,
                                ts,
                                setter,
                                wrapped_logger);
                        },
                        [logger, target_path = tmp_dir->GetPath()](
                            auto const& msg, bool fatal) {
                            (*logger)(
                                fmt::format("While running critical Git op "
                                            "INITIAL_COMMIT for target {}:\n{}",
                                            target_path.string(),
                                            msg),
                                fatal);
                        });
                }
                else {
                    // tree found, so return the git tree root as-is
                    (*setter)(std::pair(
                        nlohmann::json::array(
                            {"git tree",
                             key.hash,
                             JustMR::Utils::GetGitCacheRoot().string()}),
                        true));
                }
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
    };
    return AsyncMapConsumer<TreeIdInfo, std::pair<nlohmann::json, bool>>(
        tree_to_git, jobs);
}
