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
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>  // std::move

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/serve/mr_git_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/task_tracker.hpp"
#include "src/buildtool/system/system_command.hpp"
#include "src/other_tools/git_operations/git_ops_types.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {

void BackupToRemote(ArtifactDigest const& digest,
                    StorageConfig const& native_storage_config,
                    StorageConfig const* compat_storage_config,
                    gsl::not_null<IExecutionApi const*> const& local_api,
                    IExecutionApi const& remote_api,
                    GitTreeFetchMap::LoggerPtr const& logger) {
    // try to back up to remote CAS
    auto repo = RepositoryConfig{};
    if (repo.SetGitCAS(native_storage_config.GitRoot())) {
        auto git_api =
            MRGitApi{&repo,
                     &native_storage_config,
                     compat_storage_config,
                     compat_storage_config != nullptr ? &*local_api : nullptr};
        if (not git_api.RetrieveToCas(
                {Artifact::ObjectInfo{.digest = digest,
                                      .type = ObjectType::Tree}},
                remote_api)) {
            // give a warning
            (*logger)(fmt::format(
                          "Failed to back up tree {} from local CAS to remote",
                          digest.hash()),
                      /*fatal=*/false);
        }
    }
    else {
        // give a warning
        (*logger)(fmt::format("Failed to SetGitCAS at {}",
                              native_storage_config.GitRoot().string()),
                  /*fatal=*/false);
    }
}

/// \brief Moves the root tree from local CAS to the Git cache and sets the
/// root.
void MoveCASTreeToGit(
    HashInfo const& tree_hash,
    ArtifactDigest const& digest,  // native or compatible
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    bool backup_to_remote,
    gsl::not_null<TaskSystem*> const& ts,
    GitTreeFetchMap::SetterPtr const& setter,
    GitTreeFetchMap::LoggerPtr const& logger) {
    // Move tree from CAS to local Git storage
    auto tmp_dir =
        native_storage_config->CreateTypedTmpDir("fetch-remote-git-tree");
    if (not tmp_dir) {
        (*logger)(fmt::format("Failed to create tmp directory for copying "
                              "git-tree {} from remote CAS",
                              tree_hash.Hash()),
                  true);
        return;
    }
    if (not local_api->RetrieveToPaths(
            {Artifact::ObjectInfo{.digest = digest, .type = ObjectType::Tree}},
            {tmp_dir->GetPath()})) {
        (*logger)(fmt::format("Failed to copy git-tree {} to {}",
                              tree_hash.Hash(),
                              tmp_dir->GetPath().string()),
                  true);
        return;
    }
    CommitInfo c_info{tmp_dir->GetPath(), "tree", tree_hash.Hash()};
    import_to_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(c_info)},
        [tmp_dir,  // keep tmp_dir alive
         tree_hash,
         native_storage_config,
         compat_storage_config,
         local_api,
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
            if (backup_to_remote and remote_api != nullptr) {
                // back up only native digests, as that is what Git stores
                auto const native_digest = ArtifactDigest{tree_hash, 0};
                BackupToRemote(native_digest,
                               *native_storage_config,
                               compat_storage_config,
                               local_api,
                               *remote_api,
                               logger);
            }
            (*setter)(false /*no cache hit*/);
        },
        [logger, tmp_dir, tree_hash](auto const& msg, bool fatal) {
            (*logger)(fmt::format(
                          "While moving git-tree {} from {} to local git:\n{}",
                          tree_hash.Hash(),
                          tmp_dir->GetPath().string(),
                          msg),
                      fatal);
        });
}

void TagAndSetRoot(
    ArtifactDigest const& digest,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    bool backup_to_remote,
    gsl::not_null<TaskSystem*> const& ts,
    GitTreeFetchMap::SetterPtr const& setter,
    GitTreeFetchMap::LoggerPtr const& logger) {
    auto repo = native_storage_config->GitRoot();
    GitOpKey op_key = {.params =
                           {
                               repo,                         // target_path
                               digest.hash(),                // git_hash
                               "Keep referenced tree alive"  // message
                           },
                       .op_type = GitOpType::KEEP_TREE};
    critical_git_op_map->ConsumeAfterKeysReady(
        ts,
        {std::move(op_key)},
        [digest,
         backup_to_remote,
         native_storage_config,
         compat_storage_config,
         local_api,
         remote_api,
         logger,
         setter](auto const& values) {
            GitOpValue op_result = *values[0];
            if (not op_result.result) {
                (*logger)("Tree tagging failed",
                          /*fatal=*/true);
                return;
            }
            // backup to remote if needed and in compatibility mode
            if (backup_to_remote and remote_api != nullptr) {
                BackupToRemote(digest,
                               *native_storage_config,
                               compat_storage_config,
                               local_api,
                               *remote_api,
                               logger);
            }
            (*setter)(false /*no cache hit*/);
        },
        [logger, repo, digest](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While tagging tree {} in {} to keep it "
                                  "alive:\n{}",
                                  digest.hash(),
                                  repo.string(),
                                  msg),
                      fatal);
        });
}

void TakeTreeFromOlderGeneration(
    std::size_t generation,
    ArtifactDigest const& digest,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    GitCASPtr const& git_cas,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    bool backup_to_remote,
    gsl::not_null<TaskSystem*> const& ts,
    GitTreeFetchMap::SetterPtr const& setter,
    GitTreeFetchMap::LoggerPtr const& logger) {
    auto source = native_storage_config->GitGenerationRoot(generation);
    GitOpKey op_key = {.params =
                           {
                               source,                    // target_path
                               digest.hash(),             // git_hash
                               "Tag commit for fetching"  // message
                           },
                       .op_type = GitOpType::KEEP_TREE};
    critical_git_op_map->ConsumeAfterKeysReady(
        ts,
        {std::move(op_key)},
        [digest,
         git_cas,
         critical_git_op_map,
         local_api,
         remote_api,
         backup_to_remote,
         ts,
         setter,
         logger,
         source,
         native_storage_config,
         compat_storage_config](auto const& values) {
            GitOpValue op_result = *values[0];
            if (not op_result.result) {
                (*logger)("Tree tagging failed", /*fatal=*/true);
                return;
            }
            auto tag = *op_result.result;
            auto git_repo = GitRepoRemote::Open(git_cas);
            if (not git_repo) {
                (*logger)("Could not open main git repository",
                          /*fatal=*/true);
                return;
            }
            auto fetch_logger = std::make_shared<AsyncMapConsumerLogger>(
                [logger, tag, source](auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While fetching {} from {}:\n{}",
                                          tag,
                                          source.string(),
                                          msg),
                              fatal);
                });
            if (not git_repo->LocalFetchViaTmpRepo(
                    *native_storage_config, source, tag, fetch_logger)) {
                return;
            }
            TagAndSetRoot(digest,
                          native_storage_config,
                          compat_storage_config,
                          critical_git_op_map,
                          local_api,
                          remote_api,
                          backup_to_remote,
                          ts,
                          setter,
                          logger);
        },
        [logger, source, digest](auto const& msg, bool fatal) {
            (*logger)(
                fmt::format("While tagging tree {} in {} for fetching:\n{}",
                            source.string(),
                            digest.hash(),
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
    MirrorsPtr const& mirrors,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    bool backup_to_remote,
    gsl::not_null<JustMRProgress*> const& progress,
    std::size_t jobs) -> GitTreeFetchMap {
    auto tree_to_cache = [critical_git_op_map,
                          import_to_git_map,
                          git_bin,
                          launcher,
                          mirrors,
                          serve,
                          native_storage_config,
                          compat_storage_config,
                          local_api,
                          remote_api,
                          backup_to_remote,
                          progress](auto ts,
                                    auto setter,
                                    auto logger,
                                    auto /*unused*/,
                                    auto const& key) {
        // check whether tree exists already in Git cache;
        // ensure Git cache exists
        GitOpKey op_key = {
            .params =
                {
                    native_storage_config->GitRoot(),  // target_path
                    "",                                // git_hash
                    std::nullopt,                      // message
                    std::nullopt,                      // source_path
                    true                               // init_bare
                },
            .op_type = GitOpType::ENSURE_INIT};
        critical_git_op_map->ConsumeAfterKeysReady(
            ts,
            {std::move(op_key)},
            [critical_git_op_map,
             import_to_git_map,
             git_bin,
             launcher,
             mirrors,
             serve,
             native_storage_config,
             compat_storage_config,
             local_api,
             remote_api,
             backup_to_remote,
             key,
             progress,
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
                    (*logger)(
                        fmt::format("Could not open repository {}",
                                    native_storage_config->GitRoot().string()),
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
                auto tree_found = git_repo->CheckTreeExists(
                    key.tree_hash.Hash(), wrapped_logger);
                if (not tree_found) {
                    // errors encountered
                    return;
                }
                if (*tree_found) {
                    // backup to remote if needed
                    if (backup_to_remote and remote_api != nullptr) {
                        BackupToRemote(ArtifactDigest{key.tree_hash, 0},
                                       *native_storage_config,
                                       compat_storage_config,
                                       local_api,
                                       *remote_api,
                                       logger);
                    }
                    // success
                    (*setter)(true /*cache hit*/);
                    return;
                }

                // Check older generations for presence of the tree
                for (std::size_t generation = 1;
                     generation < native_storage_config->num_generations;
                     generation++) {
                    auto old =
                        native_storage_config->GitGenerationRoot(generation);
                    if (FileSystemManager::IsDirectory(old)) {
                        auto old_repo = GitRepo::Open(old);
                        auto no_logging =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [](auto /*unused*/, auto /*unused*/) {});
                        if (old_repo) {
                            auto check_result = old_repo->CheckTreeExists(
                                key.tree_hash.Hash(), no_logging);
                            if (check_result and *check_result) {
                                TakeTreeFromOlderGeneration(
                                    generation,
                                    ArtifactDigest{key.tree_hash, 0},
                                    native_storage_config,
                                    compat_storage_config,
                                    op_result.git_cas,
                                    critical_git_op_map,
                                    local_api,
                                    remote_api,
                                    backup_to_remote,
                                    ts,
                                    setter,
                                    logger);
                                return;
                            }
                        }
                    }
                }

                // check if tree is known to native local CAS
                auto const native_digest = ArtifactDigest{key.tree_hash, 0};
                if (local_api->IsAvailable(native_digest)) {
                    // import tree to Git cache
                    MoveCASTreeToGit(key.tree_hash,
                                     native_digest,
                                     import_to_git_map,
                                     native_storage_config,
                                     compat_storage_config,
                                     local_api,
                                     remote_api,
                                     backup_to_remote,
                                     ts,
                                     setter,
                                     logger);
                    // done!
                    return;
                }
                progress->TaskTracker().Start(key.origin);
                // check if tree is known to remote serve service and can be
                // provided via the remote CAS
                if (serve != nullptr and remote_api != nullptr) {
                    auto const remote_digest =
                        serve->TreeInRemoteCAS(key.tree_hash.Hash());
                    // try to get content from remote CAS into local CAS;
                    // whether it is retrieved locally in native or
                    // compatible CAS, it will be imported to Git either way
                    if (remote_digest and
                        remote_api->RetrieveToCas(
                            {Artifact::ObjectInfo{.digest = *remote_digest,
                                                  .type = ObjectType::Tree}},
                            *local_api)) {
                        progress->TaskTracker().Stop(key.origin);
                        MoveCASTreeToGit(key.tree_hash,
                                         *remote_digest,
                                         import_to_git_map,
                                         native_storage_config,
                                         compat_storage_config,
                                         local_api,
                                         remote_api,
                                         false,  // tree already on remote,
                                                 // so ignore backing up
                                         ts,
                                         setter,
                                         logger);
                        // done!
                        return;
                    }
                }
                // check if tree is on remote, if given and native
                if (compat_storage_config == nullptr and
                    remote_api != nullptr and
                    remote_api->RetrieveToCas(
                        {Artifact::ObjectInfo{.digest = native_digest,
                                              .type = ObjectType::Tree}},
                        *local_api)) {
                    progress->TaskTracker().Stop(key.origin);
                    MoveCASTreeToGit(key.tree_hash,
                                     native_digest,
                                     import_to_git_map,
                                     native_storage_config,
                                     compat_storage_config,
                                     local_api,
                                     remote_api,
                                     false,  // tree already on remote,
                                             // so ignore backing up
                                     ts,
                                     setter,
                                     logger);
                    // done!
                    return;
                }
                // create temporary location for command execution root
                auto content_dir =
                    native_storage_config->CreateTypedTmpDir("git-tree");
                if (not content_dir) {
                    (*logger)(
                        "Failed to create execution root tmp directory for "
                        "tree id map!",
                        /*fatal=*/true);
                    return;
                }
                // create temporary location for storing command result files
                auto out_dir =
                    native_storage_config->CreateTypedTmpDir("git-tree");
                if (not out_dir) {
                    (*logger)(
                        "Failed to create results tmp directory for tree id "
                        "map!",
                        /*fatal=*/true);
                    return;
                }
                // execute command in temporary location
                SystemCommand system{key.tree_hash.Hash()};
                auto cmdline = launcher;
                std::copy(key.command.begin(),
                          key.command.end(),
                          std::back_inserter(cmdline));
                auto inherit_env =
                    MirrorsUtils::GetInheritEnv(mirrors, key.inherit_env);
                std::map<std::string, std::string> env{key.env_vars};
                for (auto const& k : inherit_env) {
                    const char* v = std::getenv(k.c_str());
                    if (v != nullptr) {
                        env[k] = std::string(v);
                    }
                }
                auto const exit_code = system.Execute(
                    cmdline, env, content_dir->GetPath(), out_dir->GetPath());
                if (not exit_code) {
                    (*logger)(fmt::format("Failed to execute command:\n{}",
                                          nlohmann::json(cmdline).dump()),
                              /*fatal=*/true);
                    return;
                }
                // create temporary location for the import repository
                auto repo_dir =
                    native_storage_config->CreateTypedTmpDir("import-repo");
                if (not repo_dir) {
                    (*logger)(
                        "Failed to create tmp directory for import repository",
                        /*fatal=*/true);
                    return;
                }
                // do an import to git with tree check
                GitOpKey op_key = {
                    .params =
                        {
                            repo_dir->GetPath(),  // target_path
                            "",                   // git_hash
                            fmt::format("Content of tree {}",
                                        key.tree_hash.Hash()),  // message
                            content_dir->GetPath()              // source_path
                        },
                    .op_type = GitOpType::INITIAL_COMMIT};
                critical_git_op_map->ConsumeAfterKeysReady(
                    ts,
                    {std::move(op_key)},
                    [repo_dir,     // keep repo_dir alive
                     content_dir,  // keep content_dir alive
                     out_dir,      // keep stdout/stderr of command alive
                     critical_git_op_map,
                     just_git_cas = op_result.git_cas,
                     cmdline,
                     key,
                     git_bin,
                     launcher,
                     mirrors,
                     native_storage_config,
                     compat_storage_config,
                     local_api,
                     remote_api,
                     backup_to_remote,
                     progress,
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
                                            repo_dir->GetPath().string()),
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
                        auto tree_check = git_repo->CheckTreeExists(
                            key.tree_hash.Hash(), wrapped_logger);
                        if (not tree_check) {
                            // errors encountered
                            return;
                        }
                        if (not *tree_check) {
                            std::string out_str{};
                            std::string err_str{};
                            auto cmd_out = FileSystemManager::ReadFile(
                                out_dir->GetPath() / "stdout");
                            auto cmd_err = FileSystemManager::ReadFile(
                                out_dir->GetPath() / "stderr");
                            if (cmd_out) {
                                out_str = *cmd_out;
                            }
                            if (cmd_err) {
                                err_str = *cmd_err;
                            }
                            std::string output{};
                            if (not out_str.empty() or not err_str.empty()) {
                                output =
                                    fmt::format(".\nOutput of command:\n{}{}",
                                                out_str,
                                                err_str);
                            }
                            (*logger)(
                                fmt::format("Executing {} did not create "
                                            "specified tree {}{}",
                                            nlohmann::json(cmdline).dump(),
                                            key.tree_hash.Hash(),
                                            output),
                                /*fatal=*/true);
                            return;
                        }
                        auto target_path = repo_dir->GetPath();
                        // fetch all into Git cache
                        auto just_git_repo = GitRepoRemote::Open(just_git_cas);
                        if (not just_git_repo) {
                            (*logger)(
                                fmt::format(
                                    "Could not open Git repository {}",
                                    native_storage_config->GitRoot().string()),
                                /*fatal=*/true);
                            return;
                        }
                        // define temp repo path
                        auto tmp_dir = native_storage_config->CreateTypedTmpDir(
                            "git-tree");
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
                                *native_storage_config,
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
                                    native_storage_config
                                        ->GitRoot(),              // target_path
                                    *op_result.result,            // git_hash
                                    "Keep referenced tree alive"  // message
                                },
                            .op_type = GitOpType::KEEP_TAG};
                        critical_git_op_map->ConsumeAfterKeysReady(
                            ts,
                            {std::move(op_key)},
                            [remote_api,
                             native_storage_config,
                             compat_storage_config,
                             local_api,
                             backup_to_remote,
                             key,
                             progress,
                             setter,
                             logger](auto const& values) {
                                GitOpValue op_result = *values[0];
                                // check flag
                                if (not op_result.result) {
                                    (*logger)("Keep tag failed",
                                              /*fatal=*/true);
                                    return;
                                }
                                progress->TaskTracker().Stop(key.origin);
                                // backup to remote if needed and in native mode
                                if (backup_to_remote and
                                    remote_api != nullptr) {
                                    BackupToRemote(
                                        ArtifactDigest{key.tree_hash, 0},
                                        *native_storage_config,
                                        compat_storage_config,
                                        local_api,
                                        *remote_api,
                                        logger);
                                }
                                // success
                                (*setter)(false /*no cache hit*/);
                            },
                            [logger,
                             commit = *op_result.result,
                             target_path = native_storage_config->GitRoot()](
                                auto const& msg, bool fatal) {
                                (*logger)(
                                    fmt::format("While running critical Git op "
                                                "KEEP_TAG for commit {} in "
                                                "repository {}:\n{}",
                                                commit,
                                                target_path.string(),
                                                msg),
                                    fatal);
                            });
                    },
                    [logger, target_path = repo_dir->GetPath()](auto const& msg,
                                                                bool fatal) {
                        (*logger)(
                            fmt::format("While running critical Git op "
                                        "INITIAL_COMMIT for target {}:\n{}",
                                        target_path.string(),
                                        msg),
                            fatal);
                    });
            },
            [logger, target_path = native_storage_config->GitRoot()](
                auto const& msg, bool fatal) {
                (*logger)(fmt::format("While running critical Git op "
                                      "ENSURE_INIT bare for target {}:\n{}",
                                      target_path.string(),
                                      msg),
                          fatal);
            });
    };
    return AsyncMapConsumer<GitTreeInfo, bool>(tree_to_cache, jobs);
}
