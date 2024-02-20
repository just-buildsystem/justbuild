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

#include "src/other_tools/ops_maps/git_tree_fetch_map.hpp"

#include <cstdlib>

#include "fmt/core.h"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/system/system_command.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"

namespace {

void BackupToRemote(std::string const& tree_id,
                    gsl::not_null<IExecutionApi*> const& remote_api,
                    GitTreeFetchMap::LoggerPtr const& logger) {
    // try to back up to remote CAS
    auto repo = RepositoryConfig{};
    if (repo.SetGitCAS(StorageConfig::GitRoot())) {
        auto git_api = GitApi{&repo};
        if (not git_api.RetrieveToCas(
                {Artifact::ObjectInfo{
                    .digest = ArtifactDigest{tree_id, 0, /*is_tree=*/true},
                    .type = ObjectType::Tree}},
                remote_api)) {
            // give a warning
            (*logger)(fmt::format(
                          "Failed to back up tree {} from local CAS to remote",
                          tree_id),
                      /*fatal=*/false);
        }
    }
    else {
        // give a warning
        (*logger)(fmt::format("Failed to SetGitCAS at {}",
                              StorageConfig::GitRoot().string()),
                  /*fatal=*/false);
    }
}

/// \brief Moves the root tree from local CAS to the Git cache and sets the
/// root.
void MoveCASTreeToGit(
    std::string const& tree_id,
    ArtifactDigest const& digest,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<IExecutionApi*> const& local_api,
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
    bool backup_to_remote,
    gsl::not_null<TaskSystem*> const& ts,
    GitTreeFetchMap::SetterPtr const& setter,
    GitTreeFetchMap::LoggerPtr const& logger) {
    // Move tree from CAS to local Git storage
    auto tmp_dir = StorageUtils::CreateTypedTmpDir("fetch-remote-git-tree");
    if (not tmp_dir) {
        (*logger)(fmt::format("Failed to create tmp directory for copying "
                              "git-tree {} from remote CAS",
                              digest.hash()),
                  true);
        return;
    }
    if (not local_api->RetrieveToPaths(
            {Artifact::ObjectInfo{.digest = digest, .type = ObjectType::Tree}},
            {tmp_dir->GetPath()})) {
        (*logger)(fmt::format("Failed to copy git-tree {} to {}",
                              tree_id,
                              tmp_dir->GetPath().string()),
                  true);
        return;
    }
    CommitInfo c_info{tmp_dir->GetPath(), "tree", tree_id};
    import_to_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(c_info)},
        [tmp_dir,  // keep tmp_dir alive
         tree_id,
         remote_api,
         backup_to_remote,
         setter,
         logger](auto const& values) {
            if (not values[0]->second) {
                (*logger)("Importing to git failed",
                          /*fatal=*/true);
                return;
            }
            // backup to remote if needed and in compatibility mode
            if (backup_to_remote and remote_api) {
                BackupToRemote(tree_id, *remote_api, logger);
            }
            (*setter)(false /*no cache hit*/);
        },
        [logger, tmp_dir, tree_id](auto const& msg, bool fatal) {
            (*logger)(fmt::format(
                          "While moving git-tree {} from {} to local git:\n{}",
                          tree_id,
                          tmp_dir->GetPath().string(),
                          msg),
                      fatal);
        });
}

}  // namespace

auto CreateGitTreeFetchMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    bool serve_api_exists,
    gsl::not_null<IExecutionApi*> const& local_api,
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
    bool backup_to_remote,
    std::size_t jobs) -> GitTreeFetchMap {
    auto tree_to_cache = [critical_git_op_map,
                          import_to_git_map,
                          git_bin,
                          launcher,
                          serve_api_exists,
                          local_api,
                          remote_api,
                          backup_to_remote](auto ts,
                                            auto setter,
                                            auto logger,
                                            auto /*unused*/,
                                            auto const& key) {
        // check whether tree exists already in Git cache;
        // ensure Git cache exists
        GitOpKey op_key = {.params =
                               {
                                   StorageConfig::GitRoot(),  // target_path
                                   "",                        // git_hash
                                   "",                        // branch
                                   std::nullopt,              // message
                                   true                       // init_bare
                               },
                           .op_type = GitOpType::ENSURE_INIT};
        critical_git_op_map->ConsumeAfterKeysReady(
            ts,
            {std::move(op_key)},
            [critical_git_op_map,
             import_to_git_map,
             git_bin,
             launcher,
             serve_api_exists,
             local_api,
             remote_api,
             backup_to_remote,
             key,
             ts,
             setter,
             logger](auto const& values) {
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
                    (*logger)(fmt::format("Could not open repository {}",
                                          StorageConfig::GitRoot().string()),
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
                if (*tree_found) {
                    // backup to remote if needed and in native mode
                    if (backup_to_remote and remote_api) {
                        BackupToRemote(key.hash, *remote_api, logger);
                    }
                    // success
                    (*setter)(true /*cache hit*/);
                    return;
                }
                // check if tree is known to local CAS
                auto digest = ArtifactDigest{key.hash, 0, /*is_tree=*/true};
                auto const& cas = Storage::Instance().CAS();
                if (auto path = cas.TreePath(digest)) {
                    // import tree to Git cache
                    MoveCASTreeToGit(key.hash,
                                     digest,
                                     import_to_git_map,
                                     local_api,
                                     remote_api,
                                     backup_to_remote,
                                     ts,
                                     setter,
                                     logger);
                    // done!
                    return;
                }
                JustMRProgress::Instance().TaskTracker().Start(key.origin);
                // check if tree is known to remote serve service
                if (serve_api_exists) {
                    // as we anyway interrogate the remote execution endpoint,
                    // we're only interested here in the serve endpoint making
                    // an attempt to upload the tree, if known, to remote CAS
                    [[maybe_unused]] auto _ =
                        ServeApi::TreeInRemoteCAS(key.hash);
                }
                // check if tree is in remote CAS, if a remote is given
                if (remote_api and
                    remote_api.value()->RetrieveToCas(
                        {Artifact::ObjectInfo{.digest = digest,
                                              .type = ObjectType::Tree}},
                        local_api)) {
                    JustMRProgress::Instance().TaskTracker().Stop(key.origin);
                    MoveCASTreeToGit(
                        key.hash,
                        digest,
                        import_to_git_map,
                        local_api,
                        remote_api,
                        false,  // tree already in remote, so ignore backing up
                        ts,
                        setter,
                        logger);
                    // done!
                    return;
                }
                // create temporary location for command execution root
                auto tmp_dir = StorageUtils::CreateTypedTmpDir("git-tree");
                if (not tmp_dir) {
                    (*logger)(
                        "Failed to create execution root tmp directory for "
                        "tree id map!",
                        /*fatal=*/true);
                    return;
                }
                // create temporary location for storing command result files
                auto out_dir = StorageUtils::CreateTypedTmpDir("git-tree");
                if (not out_dir) {
                    (*logger)(
                        "Failed to create results tmp directory for tree id "
                        "map!",
                        /*fatal=*/true);
                    return;
                }
                // execute command in temporary location
                SystemCommand system{key.hash};
                auto cmdline = launcher;
                std::copy(key.command.begin(),
                          key.command.end(),
                          std::back_inserter(cmdline));
                std::map<std::string, std::string> env{key.env_vars};
                for (auto const& k : key.inherit_env) {
                    const char* v = std::getenv(k.c_str());
                    if (v != nullptr) {
                        env[k] = std::string(v);
                    }
                }
                auto const command_output = system.Execute(
                    cmdline, env, tmp_dir->GetPath(), out_dir->GetPath());
                if (not command_output) {
                    (*logger)(fmt::format("Failed to execute command:\n{}",
                                          nlohmann::json(cmdline).dump()),
                              /*fatal=*/true);
                    return;
                }
                // do an import to git with tree check
                GitOpKey op_key = {.params =
                                       {
                                           tmp_dir->GetPath(),  // target_path
                                           "",                  // git_hash
                                           "",                  // branch
                                           fmt::format("Content of tree {}",
                                                       key.hash),  // message
                                       },
                                   .op_type = GitOpType::INITIAL_COMMIT};
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
                     git_bin,
                     launcher,
                     remote_api,
                     backup_to_remote,
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
                                    (*logger)(fmt::format("While checking tree "
                                                          "exists:\n{}",
                                                          msg),
                                              fatal);
                                });
                        // check that the desired tree ID is part of the repo
                        auto tree_check =
                            git_repo->CheckTreeExists(key.hash, wrapped_logger);
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
                                output =
                                    fmt::format(".\nOutput of command:\n{}{}",
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
                        auto just_git_repo = GitRepoRemote::Open(just_git_cas);
                        if (not just_git_repo) {
                            (*logger)(fmt::format("Could not open Git "
                                                  "repository {}",
                                                  target_path.string()),
                                      /*fatal=*/true);
                            return;
                        }
                        // define temp repo path
                        auto tmp_dir =
                            StorageUtils::CreateTypedTmpDir("git-tree");
                        ;
                        if (not tmp_dir) {
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
                                        fmt::format("While fetch via tmp repo "
                                                    "for target {}:\n{}",
                                                    target_path.string(),
                                                    msg),
                                        fatal);
                                });
                        if (not just_git_repo->FetchViaTmpRepo(
                                tmp_dir->GetPath(),
                                target_path.string(),
                                std::nullopt,
                                key.inherit_env,
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
                        // Keep tag for commit
                        GitOpKey op_key = {
                            .params =
                                {
                                    StorageConfig::GitRoot(),     // target_path
                                    *op_result.result,            // git_hash
                                    "",                           // branch
                                    "Keep referenced tree alive"  // message
                                },
                            .op_type = GitOpType::KEEP_TAG};
                        critical_git_op_map->ConsumeAfterKeysReady(
                            ts,
                            {std::move(op_key)},
                            [tmp_dir,  // keep tmp_dir alive
                             remote_api,
                             backup_to_remote,
                             key,
                             setter,
                             logger](auto const& values) {
                                GitOpValue op_result = *values[0];
                                // check flag
                                if (not op_result.result) {
                                    (*logger)("Keep tag failed",
                                              /*fatal=*/true);
                                    return;
                                }
                                JustMRProgress::Instance().TaskTracker().Stop(
                                    key.origin);
                                // backup to remote if needed and in native mode
                                if (backup_to_remote and remote_api) {
                                    BackupToRemote(
                                        key.hash, *remote_api, logger);
                                }
                                // success
                                (*setter)(false /*no cache hit*/);
                            },
                            [logger,
                             commit = *op_result.result,
                             target_path = tmp_dir->GetPath()](auto const& msg,
                                                               bool fatal) {
                                (*logger)(
                                    fmt::format("While running critical Git op "
                                                "KEEP_TAG for commit {} in "
                                                "target {}:\n{}",
                                                commit,
                                                target_path.string(),
                                                msg),
                                    fatal);
                            });
                    },
                    [logger, target_path = tmp_dir->GetPath()](auto const& msg,
                                                               bool fatal) {
                        (*logger)(
                            fmt::format("While running critical Git op "
                                        "INITIAL_COMMIT for target {}:\n{}",
                                        target_path.string(),
                                        msg),
                            fatal);
                    });
            },
            [logger, target_path = StorageConfig::GitRoot()](auto const& msg,
                                                             bool fatal) {
                (*logger)(fmt::format("While running critical Git op "
                                      "ENSURE_INIT bare for target {}:\n{}",
                                      target_path.string(),
                                      msg),
                          fatal);
            });
    };
    return AsyncMapConsumer<GitTreeInfo, bool>(tree_to_cache, jobs);
}
