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

#include "src/other_tools/root_maps/commit_git_map.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "fmt/core.h"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/root_maps/root_utils.hpp"
#include "src/other_tools/utils/curl_url_handle.hpp"
#include "src/utils/cpp/path.hpp"

namespace {

[[nodiscard]] auto GitURLIsPath(std::string const& url) noexcept
    -> std::optional<std::string> {
    static auto const kAbsPath = std::string{"/"};
    static auto const kRelPath = std::string{"./"};
    static auto const kFileScheme = std::string{"file://"};

    if (url.starts_with(kAbsPath) or url.starts_with(kRelPath)) {
        return ToNormalPath(url).string();
    }
    if (url.starts_with(kFileScheme)) {
        return ToNormalPath(url.substr(kFileScheme.length())).string();
    }

    return std::nullopt;
}

[[nodiscard]] auto IsCacheGitRoot(
    std::filesystem::path const& repo_root) noexcept -> bool {
    return std::filesystem::absolute(ToNormalPath(repo_root)) ==
           std::filesystem::absolute(ToNormalPath(StorageConfig::GitRoot()));
}

/// \brief Helper function for ensuring the serve endpoint, if given, has the
/// root if it was marked absent.
/// It guarantees the logger is called exactly once with fatal on failure, and
/// the setter on success.
void EnsureRootAsAbsent(std::string const& tree_id,
                        std::filesystem::path const& repo_root,
                        GitRepoInfo const& repo_info,
                        std::optional<ServeApi> const& serve,
                        IExecutionApi const* remote_api,
                        CommitGitMap::SetterPtr const& ws_setter,
                        CommitGitMap::LoggerPtr const& logger) {
    // this is an absent root
    if (serve) {
        // check if the serve endpoint has this root
        auto has_tree = CheckServeHasAbsentRoot(*serve, tree_id, logger);
        if (not has_tree) {
            return;
        }
        if (not *has_tree) {
            // try to see if serve endpoint has the information to prepare the
            // root itself
            auto serve_result =
                serve->RetrieveTreeFromCommit(repo_info.hash,
                                              repo_info.subdir,
                                              /*sync_tree = */ false);
            if (std::holds_alternative<std::string>(serve_result)) {
                // if serve has set up the tree, it must match what we expect
                auto const& served_tree_id =
                    std::get<std::string>(serve_result);
                if (tree_id != served_tree_id) {
                    (*logger)(fmt::format("Mismatch in served root tree "
                                          "id:\nexpected {}, but got {}",
                                          tree_id,
                                          served_tree_id),
                              /*fatal=*/true);
                    return;
                }
            }
            else {
                // check if serve failure was due to commit not being found or
                // it is otherwise fatal
                auto const& is_fatal = std::get<bool>(serve_result);
                if (is_fatal) {
                    (*logger)(fmt::format("Serve endpoint failed to set up "
                                          "root from known commit {}",
                                          repo_info.hash),
                              /*fatal=*/true);
                    return;
                }
                if (remote_api == nullptr) {
                    (*logger)(
                        fmt::format("Missing or incompatible remote-execution "
                                    "endpoint needed to sync workspace root {} "
                                    "with the serve endpoint.",
                                    tree_id),
                        /*fatal=*/true);
                    return;
                }
                // the tree is known locally, so we can upload it to remote CAS
                // for the serve endpoint to retrieve it and set up the root
                if (not EnsureAbsentRootOnServe(*serve,
                                                tree_id,
                                                repo_root,
                                                remote_api,
                                                logger,
                                                true /*no_sync_is_fatal*/)) {
                    return;
                }
            }
        }
    }
    else {
        // give warning
        (*logger)(fmt::format("Workspace root {} marked absent but no serve "
                              "endpoint provided.",
                              tree_id),
                  /*fatal=*/false);
    }
    // set root as absent
    (*ws_setter)(std::pair(
        nlohmann::json::array({repo_info.ignore_special
                                   ? FileRoot::kGitTreeIgnoreSpecialMarker
                                   : FileRoot::kGitTreeMarker,
                               tree_id}),
        /*is_cache_hit=*/false));
}

/// \brief Helper function for improved readability.
/// It guarantees the logger is called exactly once with fatal on failure, and
/// the setter on success.
void WriteIdFileAndSetWSRoot(std::string const& root_tree_id,
                             std::string const& subdir,
                             bool ignore_special,
                             GitCASPtr const& git_cas,
                             std::filesystem::path const& tree_id_file,
                             CommitGitMap::SetterPtr const& ws_setter,
                             CommitGitMap::LoggerPtr const& logger) {
    // write association of the root tree in id file
    if (not StorageUtils::WriteTreeIDFile(tree_id_file, root_tree_id)) {
        (*logger)(fmt::format("Failed to write tree id {} to file {}",
                              root_tree_id,
                              tree_id_file.string()),
                  /*fatal=*/true);
        return;
    }
    // extract the subdir tree
    auto git_repo = GitRepoRemote::Open(git_cas);  // link fake repo to odb
    if (not git_repo) {
        (*logger)(fmt::format("Could not open cache object database {}",
                              StorageConfig::GitRoot().string()),
                  /*fatal=*/true);
        return;
    }
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger, subdir, tree = root_tree_id](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While getting subdir {} in tree {}:\n{}",
                                  subdir,
                                  tree,
                                  msg),
                      fatal);
        });
    auto tree_id =
        git_repo->GetSubtreeFromTree(root_tree_id, subdir, wrapped_logger);
    if (not tree_id) {
        return;
    }
    // set the workspace root as present
    (*ws_setter)(std::pair(
        nlohmann::json::array({ignore_special
                                   ? FileRoot::kGitTreeIgnoreSpecialMarker
                                   : FileRoot::kGitTreeMarker,
                               *tree_id,
                               StorageConfig::GitRoot().string()}),
        false));
}

void NetworkFetchAndSetPresentRoot(
    GitRepoInfo const& repo_info,
    std::filesystem::path const& repo_root,
    std::string const& fetch_repo,
    MirrorsPtr const& additional_mirrors,
    GitCASPtr const& git_cas,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    bool fetch_absent,
    gsl::not_null<TaskSystem*> const& ts,
    CommitGitMap::SetterPtr const& ws_setter,
    CommitGitMap::LoggerPtr const& logger) {
    // reaching here can only result in a root that is present
    if (repo_info.absent and not fetch_absent) {
        (*logger)(
            fmt::format("Cannot create workspace root as absent for commit {}.",
                        repo_info.hash),
            /*fatal=*/true);
        return;
    }

    auto git_repo = GitRepoRemote::Open(git_cas);  // link fake repo to odb
    if (not git_repo) {
        (*logger)(
            fmt::format("Could not open repository {}", repo_root.string()),
            /*fatal=*/true);
        return;
    }

    // store failed attempts for subsequent logging
    bool fetched{false};
    std::string err_messages{};
    // keep all remotes checked to report them in case fetch fails
    std::string remotes_buffer{};

    // try repo url
    auto all_mirrors = std::vector<std::string>({fetch_repo});
    // try repo mirrors afterwards
    all_mirrors.insert(
        all_mirrors.end(), repo_info.mirrors.begin(), repo_info.mirrors.end());

    if (auto preferred_hostnames =
            MirrorsUtils::GetPreferredHostnames(additional_mirrors);
        not preferred_hostnames.empty()) {
        all_mirrors =
            MirrorsUtils::SortByHostname(all_mirrors, preferred_hostnames);
    }

    // always try local mirrors first
    auto local_mirrors =
        MirrorsUtils::GetLocalMirrors(additional_mirrors, fetch_repo);
    all_mirrors.insert(
        all_mirrors.begin(), local_mirrors.begin(), local_mirrors.end());

    for (auto mirror : all_mirrors) {
        auto mirror_path = GitURLIsPath(mirror);
        if (mirror_path) {
            mirror = std::filesystem::absolute(*mirror_path).string();
        }
        auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
            [mirror, &err_messages](auto const& msg, bool /*fatal*/) {
                err_messages += fmt::format(
                    "While attempting fetch from URL {}:\n{}\n", mirror, msg);
            });
        if (git_repo->FetchViaTmpRepo(mirror,
                                      repo_info.branch,
                                      repo_info.inherit_env,
                                      git_bin,
                                      launcher,
                                      wrapped_logger)) {
            fetched = true;
            break;
        }
        // add local mirror to buffer
        remotes_buffer.append(fmt::format("\n> {}", mirror));
    }
    if (not fetched) {
        // log fetch failure and list the remotes tried
        (*logger)(
            fmt::format("While trying to fetch from provided remotes:{}Fetch "
                        "failed for the provided remotes{}",
                        err_messages,
                        remotes_buffer),
            /*fatal=*/true);
        return;
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While checking commit exists:\n{}", msg),
                      fatal);
        });
    // check if commit exists now, after fetch
    auto is_commit_present =
        git_repo->CheckCommitExists(repo_info.hash, wrapped_logger);
    if (not is_commit_present) {
        return;
    }
    if (not *is_commit_present) {
        // commit could not be fetched, so fail
        (*logger)(fmt::format(
                      "Could not fetch commit {} from branch {} for remote {}",
                      repo_info.hash,
                      repo_info.branch,
                      fetch_repo),
                  /*fatal=*/true);
        return;
    }
    // if witnessing repository is the Git cache, then also tag the commit
    if (IsCacheGitRoot(repo_root)) {
        GitOpKey op_key = {.params =
                               {
                                   repo_root,                    // target_path
                                   repo_info.hash,               // git_hash
                                   "",                           // branch
                                   "Keep referenced tree alive"  // message
                               },
                           .op_type = GitOpType::KEEP_TAG};
        critical_git_op_map->ConsumeAfterKeysReady(
            ts,
            {std::move(op_key)},
            [git_cas, repo_info, repo_root, ws_setter, logger](
                auto const& values) {
                GitOpValue op_result = *values[0];
                // check flag
                if (not op_result.result) {
                    (*logger)("Keep tag failed",
                              /*fatal=*/true);
                    return;
                }
                auto git_repo =
                    GitRepoRemote::Open(git_cas);  // link fake repo to odb
                if (not git_repo) {
                    (*logger)(fmt::format("Could not open repository {}",
                                          repo_root.string()),
                              /*fatal=*/true);
                    return;
                }
                // setup wrapped logger
                auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                    [logger](auto const& msg, bool fatal) {
                        (*logger)(
                            fmt::format(
                                "While getting subtree from commit:\n{}", msg),
                            fatal);
                    });
                // get tree id and return workspace root
                auto res = git_repo->GetSubtreeFromCommit(
                    repo_info.hash, repo_info.subdir, wrapped_logger);
                if (not std::holds_alternative<std::string>(res)) {
                    return;
                }
                // set the workspace root as present
                JustMRProgress::Instance().TaskTracker().Stop(repo_info.origin);
                (*ws_setter)(
                    std::pair(nlohmann::json::array(
                                  {repo_info.ignore_special
                                       ? FileRoot::kGitTreeIgnoreSpecialMarker
                                       : FileRoot::kGitTreeMarker,
                                   std::get<std::string>(res),  // subtree id
                                   repo_root}),
                              /*is_cache_hit=*/false));
            },
            [logger, target_path = repo_root](auto const& msg, bool fatal) {
                (*logger)(fmt::format("While running critical Git op KEEP_TAG "
                                      "for target {}:\n{}",
                                      target_path.string(),
                                      msg),
                          fatal);
            });
    }
    else {
        auto git_repo = GitRepoRemote::Open(git_cas);  // link fake repo to odb
        if (not git_repo) {
            (*logger)(
                fmt::format("Could not open repository {}", repo_root.string()),
                /*fatal=*/true);
            return;
        }
        // setup wrapped logger
        auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
            [logger](auto const& msg, bool fatal) {
                (*logger)(
                    fmt::format("While getting subtree from commit:\n{}", msg),
                    fatal);
            });
        // get tree id and return workspace root
        auto res = git_repo->GetSubtreeFromCommit(
            repo_info.hash, repo_info.subdir, wrapped_logger);
        if (not std::holds_alternative<std::string>(res)) {
            return;
        }
        // set the workspace root as present
        JustMRProgress::Instance().TaskTracker().Stop(repo_info.origin);
        (*ws_setter)(std::pair(
            nlohmann::json::array({repo_info.ignore_special
                                       ? FileRoot::kGitTreeIgnoreSpecialMarker
                                       : FileRoot::kGitTreeMarker,
                                   std::get<std::string>(res),  // subtree id
                                   repo_root}),
            /*is_cache_hit=*/false));
    }
}

/// \brief Contains the main logic for this async map. It ensures the commit is
/// available for processing (including fetching for a present root) and setting
/// the root.
/// It guarantees the logger is called exactly once with fatal on failure, and
/// the setter on success.
void EnsureCommit(GitRepoInfo const& repo_info,
                  std::filesystem::path const& repo_root,
                  std::string const& fetch_repo,
                  MirrorsPtr const& additional_mirrors,
                  GitCASPtr const& git_cas,
                  gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
                  gsl::not_null<ImportToGitMap*> const& import_to_git_map,
                  std::string const& git_bin,
                  std::vector<std::string> const& launcher,
                  std::optional<ServeApi> const& serve,
                  gsl::not_null<IExecutionApi const*> const& local_api,
                  IExecutionApi const* remote_api,
                  bool fetch_absent,
                  gsl::not_null<TaskSystem*> const& ts,
                  CommitGitMap::SetterPtr const& ws_setter,
                  CommitGitMap::LoggerPtr const& logger) {
    // link fake repo to odb
    auto git_repo = GitRepoRemote::Open(git_cas);
    if (not git_repo) {
        (*logger)(
            fmt::format("Could not open repository {}", repo_root.string()),
            /*fatal=*/true);
        return;
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While checking commit exists:\n{}", msg),
                      fatal);
        });
    auto is_commit_present =
        git_repo->CheckCommitExists(repo_info.hash, wrapped_logger);
    if (is_commit_present == std::nullopt) {
        return;
    }
    if (not is_commit_present.value()) {
        auto tree_id_file = StorageUtils::GetCommitTreeIDFile(repo_info.hash);
        // Check if we have stored a file association between commit and tree;
        // if an association file exists, the respective tree MUST be in the
        // Git cache
        if (FileSystemManager::Exists(tree_id_file)) {
            // read resolved tree id
            auto resolved_tree_id = FileSystemManager::ReadFile(tree_id_file);
            if (not resolved_tree_id) {
                (*logger)(fmt::format("Failed to read tree id from file {}",
                                      tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            auto just_git_cas = GitCAS::Open(StorageConfig::GitRoot());
            if (not just_git_cas) {
                (*logger)(fmt::format("Could not open Git cache database {}",
                                      StorageConfig::GitRoot().string()),
                          /*fatal=*/true);
                return;
            }
            auto just_git_repo = GitRepo::Open(just_git_cas);
            if (not just_git_repo) {
                (*logger)(fmt::format("Could not open Git cache repository {}",
                                      StorageConfig::GitRoot().string()),
                          /*fatal=*/true);
                return;
            }
            // extract the subdir tree
            wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [logger, subdir = repo_info.subdir, tree = *resolved_tree_id](
                    auto const& msg, bool fatal) {
                    (*logger)(
                        fmt::format("While getting subdir {} in tree {}:\n{}",
                                    subdir,
                                    tree,
                                    msg),
                        fatal);
                });
            auto tree_id = just_git_repo->GetSubtreeFromTree(
                *resolved_tree_id, repo_info.subdir, wrapped_logger);
            if (not tree_id) {
                return;
            }
            // set the workspace root
            if (repo_info.absent and not fetch_absent) {
                // try by all available means to generate & set the absent root
                EnsureRootAsAbsent(*tree_id,
                                   StorageConfig::GitRoot(),
                                   repo_info,
                                   serve,
                                   remote_api,
                                   ws_setter,
                                   logger);
            }
            else {
                // this root is present
                (*ws_setter)(
                    std::pair(nlohmann::json::array(
                                  {repo_info.ignore_special
                                       ? FileRoot::kGitTreeIgnoreSpecialMarker
                                       : FileRoot::kGitTreeMarker,
                                   *tree_id,
                                   StorageConfig::GitRoot().string()}),
                              /*is_cache_hit=*/false));
            }
            // done!
            return;
        }

        // no id file association exists
        JustMRProgress::Instance().TaskTracker().Start(repo_info.origin);
        // check if commit is known to remote serve service
        if (serve) {
            // if root purely absent, request only the subdir tree
            if (repo_info.absent and not fetch_absent) {
                auto serve_result =
                    serve->RetrieveTreeFromCommit(repo_info.hash,
                                                  repo_info.subdir,
                                                  /*sync_tree = */ false);
                if (std::holds_alternative<std::string>(serve_result)) {
                    // set the workspace root as absent
                    JustMRProgress::Instance().TaskTracker().Stop(
                        repo_info.origin);
                    (*ws_setter)(std::pair(
                        nlohmann::json::array(
                            {repo_info.ignore_special
                                 ? FileRoot::kGitTreeIgnoreSpecialMarker
                                 : FileRoot::kGitTreeMarker,
                             std::get<std::string>(serve_result)}),
                        /*is_cache_hit=*/false));
                    return;
                }
                // check if serve failure was due to commit not being found or
                // it is otherwise fatal
                auto const& is_fatal = std::get<bool>(serve_result);
                if (is_fatal) {
                    (*logger)(fmt::format("Serve endpoint failed to set up "
                                          "root from known commit {}",
                                          repo_info.hash),
                              /*fatal=*/true);
                    return;
                }
            }
            // otherwise, request (and sync) the whole commit tree, to ensure
            // we maintain the id file association
            else {
                auto serve_result =
                    serve->RetrieveTreeFromCommit(repo_info.hash,
                                                  /*subdir = */ ".",
                                                  /*sync_tree = */ true);
                if (std::holds_alternative<std::string>(serve_result)) {
                    auto const& root_tree_id =
                        std::get<std::string>(serve_result);
                    // verify if we know the tree already in the local Git cache
                    GitOpKey op_key = {
                        .params =
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
                        [root_tree_id,
                         tree_id_file,
                         repo_info,
                         repo_root,
                         fetch_repo,
                         additional_mirrors,
                         git_cas,
                         critical_git_op_map,
                         import_to_git_map,
                         git_bin,
                         launcher,
                         local_api,
                         remote_api,
                         fetch_absent,
                         ts,
                         ws_setter,
                         logger](auto const& values) {
                            GitOpValue op_result = *values[0];
                            // check flag
                            if (not op_result.result) {
                                (*logger)("Git init failed",
                                          /*fatal=*/true);
                                return;
                            }
                            auto just_git_repo =
                                GitRepoRemote::Open(op_result.git_cas);
                            if (not just_git_repo) {
                                (*logger)(
                                    fmt::format(
                                        "Could not open Git cache repository "
                                        "{}",
                                        StorageConfig::GitRoot().string()),
                                    /*fatal=*/true);
                                return;
                            }
                            // check tree existence
                            auto wrapped_logger =
                                std::make_shared<AsyncMapConsumerLogger>(
                                    [logger, tree = root_tree_id](
                                        auto const& msg, bool fatal) {
                                        (*logger)(
                                            fmt::format(
                                                "While verifying presence of "
                                                "tree {} in repository {}:\n{}",
                                                tree,
                                                StorageConfig::GitRoot()
                                                    .string(),
                                                msg),
                                            fatal);
                                    });
                            auto tree_present = just_git_repo->CheckTreeExists(
                                root_tree_id, wrapped_logger);
                            if (not tree_present) {
                                return;
                            }
                            if (*tree_present) {
                                JustMRProgress::Instance().TaskTracker().Stop(
                                    repo_info.origin);
                                // write association to id file, get subdir
                                // tree, and set the workspace root as present
                                WriteIdFileAndSetWSRoot(
                                    root_tree_id,
                                    repo_info.subdir,
                                    repo_info.ignore_special,
                                    op_result.git_cas,
                                    tree_id_file,
                                    ws_setter,
                                    logger);
                                return;
                            }

                            // now check if the tree is in the local checkout,
                            // if this checkout is not our Git cache; this can
                            // save an unnecessary remote CAS call
                            if (not IsCacheGitRoot(repo_root)) {
                                auto git_repo = GitRepoRemote::Open(git_cas);
                                if (not git_repo) {
                                    (*logger)(fmt::format("Could not open Git "
                                                          "repository {}",
                                                          repo_root.string()),
                                              /*fatal=*/true);
                                    return;
                                }
                                // check tree existence
                                wrapped_logger =
                                    std::make_shared<AsyncMapConsumerLogger>(
                                        [logger,
                                         tree = root_tree_id,
                                         repo_root](auto const& msg,
                                                    bool fatal) {
                                            (*logger)(
                                                fmt::format(
                                                    "While verifying presence "
                                                    "of tree {} in repository "
                                                    "{}:\n{}",
                                                    tree,
                                                    repo_root.string(),
                                                    msg),
                                                fatal);
                                        });
                                tree_present = git_repo->CheckTreeExists(
                                    root_tree_id, wrapped_logger);
                                if (not tree_present) {
                                    return;
                                }
                                if (*tree_present) {
                                    JustMRProgress::Instance()
                                        .TaskTracker()
                                        .Stop(repo_info.origin);
                                    // get subdir tree and set the workspace
                                    // root as present; as this tree is not in
                                    // our Git cache, no file association should
                                    // be stored
                                    wrapped_logger = std::make_shared<
                                        AsyncMapConsumerLogger>(
                                        [logger,
                                         subdir = repo_info.subdir,
                                         tree = root_tree_id](auto const& msg,
                                                              bool fatal) {
                                            (*logger)(
                                                fmt::format(
                                                    "While getting subdir {} "
                                                    "in tree {}:\n{}",
                                                    subdir,
                                                    tree,
                                                    msg),
                                                fatal);
                                        });
                                    auto tree_id = git_repo->GetSubtreeFromTree(
                                        root_tree_id,
                                        repo_info.subdir,
                                        wrapped_logger);
                                    if (not tree_id) {
                                        return;
                                    }
                                    // set the workspace root as present
                                    (*ws_setter)(std::pair(
                                        nlohmann::json::array(
                                            {repo_info.ignore_special
                                                 ? FileRoot::
                                                       kGitTreeIgnoreSpecialMarker
                                                 : FileRoot::kGitTreeMarker,
                                             *tree_id,
                                             repo_root.string()}),
                                        false));
                                    // done!
                                    return;
                                }
                            }

                            // try to get root tree from remote CAS
                            auto root_digest = ArtifactDigest{
                                root_tree_id, 0, /*is_tree=*/true};
                            if (remote_api != nullptr and
                                remote_api->RetrieveToCas(
                                    {Artifact::ObjectInfo{
                                        .digest = root_digest,
                                        .type = ObjectType::Tree}},
                                    *local_api)) {
                                JustMRProgress::Instance().TaskTracker().Stop(
                                    repo_info.origin);
                                // Move tree from local CAS to local Git storage
                                auto tmp_dir = StorageConfig::CreateTypedTmpDir(
                                    "fetch-absent-root");
                                if (not tmp_dir) {
                                    (*logger)(
                                        fmt::format(
                                            "Failed to create tmp directory "
                                            "after fetching root tree {} for "
                                            "absent commit {}",
                                            root_tree_id,
                                            repo_info.hash),
                                        /*fatal=*/true);
                                    return;
                                }
                                if (not local_api->RetrieveToPaths(
                                        {Artifact::ObjectInfo{
                                            .digest = root_digest,
                                            .type = ObjectType::Tree}},
                                        {tmp_dir->GetPath()})) {
                                    (*logger)(fmt::format(
                                                  "Failed to copy fetched root "
                                                  "tree {} to {}",
                                                  root_tree_id,
                                                  tmp_dir->GetPath().string()),
                                              /*fatal=*/true);
                                    return;
                                }
                                CommitInfo c_info{
                                    tmp_dir->GetPath(), "tree", root_tree_id};
                                import_to_git_map->ConsumeAfterKeysReady(
                                    ts,
                                    {std::move(c_info)},
                                    [tmp_dir,  // keep tmp_dir alive
                                     root_tree_id,
                                     subdir = repo_info.subdir,
                                     ignore_special = repo_info.ignore_special,
                                     just_git_cas = op_result.git_cas,
                                     tree_id_file,
                                     ws_setter,
                                     logger](auto const& values) {
                                        if (not values[0]->second) {
                                            (*logger)("Importing to git failed",
                                                      /*fatal=*/true);
                                            return;
                                        }
                                        // sanity check: we should get the
                                        // expected tree
                                        if (values[0]->first != root_tree_id) {
                                            (*logger)(
                                                fmt::format(
                                                    "Mismatch in imported git "
                                                    "tree id:\nexpected {}, "
                                                    "but got {}",
                                                    root_tree_id,
                                                    values[0]->first),
                                                /*fatal=*/true);
                                            return;
                                        }
                                        // tree is now in Git cache;
                                        // write association to id file, get
                                        // subdir tree, and set the workspace
                                        // root as present
                                        WriteIdFileAndSetWSRoot(root_tree_id,
                                                                subdir,
                                                                ignore_special,
                                                                just_git_cas,
                                                                tree_id_file,
                                                                ws_setter,
                                                                logger);
                                    },
                                    [logger, tmp_dir, root_tree_id](
                                        auto const& msg, bool fatal) {
                                        (*logger)(
                                            fmt::format(
                                                "While moving root tree {} "
                                                "from {} to local git:\n{}",
                                                root_tree_id,
                                                tmp_dir->GetPath().string(),
                                                msg),
                                            fatal);
                                    });

                                return;
                            }
                            // just serve should have made the tree available in
                            // the remote CAS, so log this attempt and revert to
                            // network
                            (*logger)(fmt::format("Tree {} marked as served, "
                                                  "but not found on remote",
                                                  root_tree_id),
                                      /*fatal=*/false);

                            NetworkFetchAndSetPresentRoot(repo_info,
                                                          repo_root,
                                                          fetch_repo,
                                                          additional_mirrors,
                                                          git_cas,
                                                          critical_git_op_map,
                                                          git_bin,
                                                          launcher,
                                                          fetch_absent,
                                                          ts,
                                                          ws_setter,
                                                          logger);
                        },
                        [logger, target_path = StorageConfig::GitRoot()](
                            auto const& msg, bool fatal) {
                            (*logger)(fmt::format("While running critical Git "
                                                  "op ENSURE_INIT bare for "
                                                  "target {}:\n{}",
                                                  target_path.string(),
                                                  msg),
                                      fatal);
                        });

                    // done!
                    return;
                }

                // check if serve failure was due to commit not being found
                // or it is otherwise fatal
                auto const& is_fatal = std::get<bool>(serve_result);
                if (is_fatal) {
                    (*logger)(fmt::format("Serve endpoint failed to set up "
                                          "root from known commit {}",
                                          repo_info.hash),
                              /*fatal=*/true);
                    return;
                }
            }
        }

        NetworkFetchAndSetPresentRoot(repo_info,
                                      repo_root,
                                      fetch_repo,
                                      additional_mirrors,
                                      git_cas,
                                      critical_git_op_map,
                                      git_bin,
                                      launcher,
                                      fetch_absent,
                                      ts,
                                      ws_setter,
                                      logger);
    }
    else {
        // commit is present in given repository
        JustMRProgress::Instance().TaskTracker().Start(repo_info.origin);
        // setup wrapped logger
        auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
            [logger](auto const& msg, bool fatal) {
                (*logger)(
                    fmt::format("While getting subtree from commit:\n{}", msg),
                    fatal);
            });
        // get tree id and return workspace root
        auto res = git_repo->GetSubtreeFromCommit(
            repo_info.hash, repo_info.subdir, wrapped_logger);
        if (not std::holds_alternative<std::string>(res)) {
            return;
        }
        auto subtree = std::get<std::string>(res);
        // set the workspace root
        if (repo_info.absent and not fetch_absent) {
            // try by all available means to generate and set the absent root
            EnsureRootAsAbsent(subtree,
                               repo_root,
                               repo_info,
                               serve,
                               remote_api,
                               ws_setter,
                               logger);
        }
        else {
            // set root as present
            (*ws_setter)(
                std::pair(nlohmann::json::array(
                              {repo_info.ignore_special
                                   ? FileRoot::kGitTreeIgnoreSpecialMarker
                                   : FileRoot::kGitTreeMarker,
                               subtree,
                               repo_root.string()}),
                          /*is_cache_hit=*/true));
        }
    }
}

}  // namespace

/// \brief Create a CommitGitMap object
auto CreateCommitGitMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    LocalPathsPtr const& just_mr_paths,
    MirrorsPtr const& additional_mirrors,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    std::optional<ServeApi> const& serve,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    bool fetch_absent,
    std::size_t jobs) -> CommitGitMap {
    auto commit_to_git = [critical_git_op_map,
                          import_to_git_map,
                          just_mr_paths,
                          additional_mirrors,
                          git_bin,
                          launcher,
                          &serve,
                          local_api,
                          remote_api,
                          fetch_absent](auto ts,
                                        auto setter,
                                        auto logger,
                                        auto /* unused */,
                                        auto const& key) {
        // get root for repo (making sure that if repo is a path, it is
        // absolute)
        std::string fetch_repo = key.repo_url;
        auto fetch_repo_path = GitURLIsPath(fetch_repo);
        if (fetch_repo_path) {
            fetch_repo = std::filesystem::absolute(*fetch_repo_path).string();
        }
        std::filesystem::path repo_root =
            StorageUtils::GetGitRoot(just_mr_paths, fetch_repo);
        // ensure git repo
        // define Git operation to be done
        GitOpKey op_key = {
            .params =
                {
                    repo_root,     // target_path
                    "",            // git_hash
                    "",            // branch
                    std::nullopt,  // message
                    not just_mr_paths->git_checkout_locations.contains(
                        fetch_repo)  // init_bare
                },
            .op_type = GitOpType::ENSURE_INIT};
        critical_git_op_map->ConsumeAfterKeysReady(
            ts,
            {std::move(op_key)},
            [key,
             repo_root,
             fetch_repo,
             additional_mirrors,
             critical_git_op_map,
             import_to_git_map,
             git_bin,
             launcher,
             &serve,
             local_api,
             remote_api,
             fetch_absent,
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
                // setup a wrapped_logger
                auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                    [logger, target_path = repo_root](auto const& msg,
                                                      bool fatal) {
                        (*logger)(fmt::format("While ensuring commit for "
                                              "repository {}:\n{}",
                                              target_path.string(),
                                              msg),
                                  fatal);
                    });
                EnsureCommit(key,
                             repo_root,
                             fetch_repo,
                             additional_mirrors,
                             op_result.git_cas,
                             critical_git_op_map,
                             import_to_git_map,
                             git_bin,
                             launcher,
                             serve,
                             local_api,
                             remote_api,
                             fetch_absent,
                             ts,
                             setter,
                             wrapped_logger);
            },
            [logger, target_path = repo_root](auto const& msg, bool fatal) {
                (*logger)(fmt::format("While running critical Git op "
                                      "ENSURE_INIT for target {}:\n{}",
                                      target_path.string(),
                                      msg),
                          fatal);
            });
    };
    return AsyncMapConsumer<GitRepoInfo, std::pair<nlohmann::json, bool>>(
        commit_to_git, jobs);
}
