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

#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/utils/curl_url_handle.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {

[[nodiscard]] auto GitURLIsPath(std::string const& url) noexcept -> bool {
    auto prefixes = std::vector<std::string>{"ssh://", "http://", "https://"};
    // return true if no URL prefix exists
    return std::none_of(
        prefixes.begin(), prefixes.end(), [url](auto const& prefix) {
            return (url.rfind(prefix, 0) == 0);
        });
}

// Helper function for improved readability. It guarantees the logger is
// called exactly once with fatal if failure.
void WriteIdFileAndSetWSRoot(std::string const& root_tree_id,
                             std::string const& subdir,
                             bool ignore_special,
                             GitCASPtr const& git_cas,
                             std::filesystem::path const& repo_root,
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
        (*logger)(
            fmt::format("Could not open repository {}", repo_root.string()),
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

void EnsureCommit(GitRepoInfo const& repo_info,
                  std::filesystem::path const& repo_root,
                  std::string const& fetch_repo,
                  MirrorsPtr const& additional_mirrors,
                  GitCASPtr const& git_cas,
                  gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
                  gsl::not_null<ImportToGitMap*> const& import_to_git_map,
                  std::string const& git_bin,
                  std::vector<std::string> const& launcher,
                  bool serve_api_exists,
                  IExecutionApi* local_api,
                  IExecutionApi* remote_api,
                  bool fetch_absent,
                  gsl::not_null<TaskSystem*> const& ts,
                  CommitGitMap::SetterPtr const& ws_setter,
                  CommitGitMap::LoggerPtr const& logger) {
    // ensure commit exists, and fetch if needed
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
            (*logger)(fmt::format("While checking commit exists:\n{}", msg),
                      fatal);
        });
    auto is_commit_present =
        git_repo->CheckCommitExists(repo_info.hash, wrapped_logger);
    if (is_commit_present == std::nullopt) {
        return;
    }
    if (not is_commit_present.value()) {
        if (repo_info.absent) {
            auto tree_id_file =
                StorageUtils::GetCommitTreeIDFile(repo_info.hash);
            if (FileSystemManager::Exists(tree_id_file)) {
                // read resolved tree id
                auto resolved_tree_id =
                    FileSystemManager::ReadFile(tree_id_file);
                if (not resolved_tree_id) {
                    (*logger)(fmt::format("Failed to read tree id from file {}",
                                          tree_id_file.string()),
                              /*fatal=*/true);
                    return;
                }
                // extract the subdir tree
                wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                    [logger,
                     subdir = repo_info.subdir,
                     tree = *resolved_tree_id](auto const& msg, bool fatal) {
                        (*logger)(fmt::format(
                                      "While getting subdir {} in tree {}:\n{}",
                                      subdir,
                                      tree,
                                      msg),
                                  fatal);
                    });
                auto tree_id = git_repo->GetSubtreeFromTree(
                    *resolved_tree_id, repo_info.subdir, wrapped_logger);
                if (not tree_id) {
                    return;
                }
                // set the workspace root
                if (fetch_absent) {
                    (*ws_setter)(std::pair(
                        nlohmann::json::array(
                            {repo_info.ignore_special
                                 ? FileRoot::kGitTreeIgnoreSpecialMarker
                                 : FileRoot::kGitTreeMarker,
                             *tree_id,
                             StorageConfig::GitRoot().string()}),
                        false));
                }
                else {
                    (*ws_setter)(std::pair(
                        nlohmann::json::array(
                            {repo_info.ignore_special
                                 ? FileRoot::kGitTreeIgnoreSpecialMarker
                                 : FileRoot::kGitTreeMarker,
                             *tree_id}),
                        false));
                }
                return;
            }
            JustMRProgress::Instance().TaskTracker().Start(repo_info.origin);
            // check if commit is known to remote serve service, if asked for an
            // absent root
            if (serve_api_exists) {
                // if fetching absent, request (and sync) the whole commit tree,
                // to ensure we maintain the id file association
                if (fetch_absent) {
                    if (auto root_tree_id = ServeApi::RetrieveTreeFromCommit(
                            repo_info.hash,
                            /*subdir = */ ".",
                            /*sync_tree = */ true)) {
                        // verify if we know the tree already locally
                        auto wrapped_logger =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [logger, tree = *root_tree_id](auto const& msg,
                                                               bool fatal) {
                                    (*logger)(fmt::format(
                                                  "While verifying presence of "
                                                  "tree {}:\n{}",
                                                  tree,
                                                  msg),
                                              fatal);
                                });
                        auto tree_present = git_repo->CheckTreeExists(
                            *root_tree_id, wrapped_logger);
                        if (not tree_present) {
                            return;
                        }
                        if (*tree_present) {
                            JustMRProgress::Instance().TaskTracker().Stop(
                                repo_info.origin);
                            // write association to id file, get subdir tree,
                            // and set the workspace root
                            WriteIdFileAndSetWSRoot(*root_tree_id,
                                                    repo_info.subdir,
                                                    repo_info.ignore_special,
                                                    git_cas,
                                                    repo_root,
                                                    tree_id_file,
                                                    ws_setter,
                                                    logger);
                            return;
                        }
                        // try to get root tree from remote execution endpoint
                        auto root_digest =
                            ArtifactDigest{*root_tree_id, 0, /*is_tree=*/true};
                        if (remote_api != nullptr and local_api != nullptr and
                            remote_api->RetrieveToCas(
                                {Artifact::ObjectInfo{
                                    .digest = root_digest,
                                    .type = ObjectType::Tree}},
                                local_api)) {
                            JustMRProgress::Instance().TaskTracker().Stop(
                                repo_info.origin);
                            // Move tree from CAS to local git storage
                            auto tmp_dir = StorageUtils::CreateTypedTmpDir(
                                "fetch-absent-root");
                            if (not tmp_dir) {
                                (*logger)(
                                    fmt::format("Failed to create tmp "
                                                "directory after fetching root "
                                                "tree {} for absent commit {}",
                                                *root_tree_id,
                                                repo_info.hash),
                                    true);
                                return;
                            }
                            if (not local_api->RetrieveToPaths(
                                    {Artifact::ObjectInfo{
                                        .digest = root_digest,
                                        .type = ObjectType::Tree}},
                                    {tmp_dir->GetPath()})) {
                                (*logger)(
                                    fmt::format("Failed to copy fetched root "
                                                "tree {} to {}",
                                                *root_tree_id,
                                                tmp_dir->GetPath().string()),
                                    true);
                                return;
                            }
                            CommitInfo c_info{
                                tmp_dir->GetPath(), "tree", *root_tree_id};
                            import_to_git_map->ConsumeAfterKeysReady(
                                ts,
                                {std::move(c_info)},
                                [tmp_dir,  // keep tmp_dir alive
                                 root_tree_id = *root_tree_id,
                                 subdir = repo_info.subdir,
                                 ignore_special = repo_info.ignore_special,
                                 git_cas,
                                 repo_root,
                                 tree_id_file,
                                 ws_setter,
                                 logger](auto const& values) {
                                    if (not values[0]->second) {
                                        (*logger)("Importing to git failed",
                                                  /*fatal=*/true);
                                        return;
                                    }
                                    // write association to id file, get subdir
                                    // tree, and set the workspace root
                                    WriteIdFileAndSetWSRoot(root_tree_id,
                                                            subdir,
                                                            ignore_special,
                                                            git_cas,
                                                            repo_root,
                                                            tree_id_file,
                                                            ws_setter,
                                                            logger);
                                },
                                [logger, tmp_dir, root_tree_id](auto const& msg,
                                                                bool fatal) {
                                    (*logger)(
                                        fmt::format(
                                            "While moving root tree {} from {} "
                                            "to local git:\n{}",
                                            *root_tree_id,
                                            tmp_dir->GetPath().string(),
                                            msg),
                                        fatal);
                                });

                            return;
                        }
                        // just serve should have made the tree available in the
                        // remote CAS, so log this attempt and revert to network
                        (*logger)(
                            fmt::format("Tree {} marked as served, but not "
                                        "found on remote",
                                        *root_tree_id),
                            /*fatal=*/false);
                    }
                    else {
                        // give warning
                        (*logger)(fmt::format("Root tree for commit {} could "
                                              "not be served",
                                              repo_info.hash),
                                  /*fatal=*/false);
                    }
                }
                // if not fetching absent, request the subdir tree directly
                else {
                    if (auto tree_id = ServeApi::RetrieveTreeFromCommit(
                            repo_info.hash,
                            repo_info.subdir,
                            /*sync_tree = */ false)) {
                        // set the workspace root as absent
                        JustMRProgress::Instance().TaskTracker().Stop(
                            repo_info.origin);
                        (*ws_setter)(std::pair(
                            nlohmann::json::array(
                                {repo_info.ignore_special
                                     ? FileRoot::kGitTreeIgnoreSpecialMarker
                                     : FileRoot::kGitTreeMarker,
                                 *tree_id}),
                            false));
                        return;
                    }
                    // give warning
                    (*logger)(fmt::format("Tree at subdir {} for commit {} "
                                          "could not be served",
                                          repo_info.subdir,
                                          repo_info.hash),
                              /*fatal=*/false);
                }
            }
            else {
                if (not fetch_absent) {
                    // give warning
                    (*logger)(fmt::format("Missing serve endpoint for Git "
                                          "repository {} marked absent "
                                          "requires slower network fetch.",
                                          repo_root.string()),
                              /*fatal=*/false);
                }
            }
        }
        // default to fetching it from network
        auto tmp_dir = StorageUtils::CreateTypedTmpDir("fetch");
        if (not tmp_dir) {
            (*logger)("Failed to create fetch tmp directory!",
                      /*fatal=*/true);
            return;
        }
        // store failed attempts for subsequent logging
        bool fetched{false};
        std::string err_messages{};
        // try local mirrors first
        auto local_mirrors =
            MirrorsUtils::GetLocalMirrors(additional_mirrors, fetch_repo);
        for (auto mirror : local_mirrors) {
            if (GitURLIsPath(mirror)) {
                mirror = std::filesystem::absolute(mirror).string();
            }
            auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [mirror, &err_messages](auto const& msg, bool /*fatal*/) {
                    err_messages += fmt::format(
                        "\nWhile attempting fetch from local mirror "
                        "{}:\n{}",
                        mirror,
                        msg);
                });
            if (git_repo->FetchViaTmpRepo(tmp_dir->GetPath(),
                                          mirror,
                                          repo_info.branch,
                                          git_bin,
                                          launcher,
                                          wrapped_logger)) {
                fetched = true;
                break;
            }
        }
        if (not fetched) {
            // get preferred hostnames list
            auto preferred_hostnames =
                MirrorsUtils::GetPreferredHostnames(additional_mirrors);
            // try first the main URL, but with each of the preferred hostnames,
            // if URL is not a path
            if (not GitURLIsPath(fetch_repo)) {
                for (auto const& hostname : preferred_hostnames) {
                    if (auto preferred_url = CurlURLHandle::ReplaceHostname(
                            fetch_repo, hostname)) {
                        auto wrapped_logger =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [preferred_url, &err_messages](auto const& msg,
                                                               bool /*fatal*/) {
                                    err_messages += fmt::format(
                                        "\nWhile attempting fetch from remote "
                                        "{}:\n{}",
                                        *preferred_url,
                                        msg);
                                });
                        if (git_repo->FetchViaTmpRepo(tmp_dir->GetPath(),
                                                      *preferred_url,
                                                      repo_info.branch,
                                                      git_bin,
                                                      launcher,
                                                      wrapped_logger)) {
                            fetched = true;
                            break;
                        }
                    }
                }
            }
            if (not fetched) {
                // now try the original main fetch URL
                auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                    [fetch_repo, &err_messages](auto const& msg,
                                                bool /*fatal*/) {
                        err_messages += fmt::format(
                            "\nWhile attempting fetch from remote {}:\n{}",
                            fetch_repo,
                            msg);
                    });
                if (not git_repo->FetchViaTmpRepo(tmp_dir->GetPath(),
                                                  fetch_repo,
                                                  repo_info.branch,
                                                  git_bin,
                                                  launcher,
                                                  wrapped_logger)) {
                    // now try to fetch from mirrors, in order, if given
                    for (auto mirror : repo_info.mirrors) {
                        if (GitURLIsPath(mirror)) {
                            mirror = std::filesystem::absolute(mirror).string();
                        }
                        else {
                            // if non-path, try each of the preferred hostnames
                            for (auto const& hostname : preferred_hostnames) {
                                if (auto preferred_mirror =
                                        CurlURLHandle::ReplaceHostname(
                                            mirror, hostname)) {
                                    wrapped_logger = std::make_shared<
                                        AsyncMapConsumerLogger>(
                                        [preferred_mirror, &err_messages](
                                            auto const& msg, bool /*fatal*/) {
                                            err_messages += fmt::format(
                                                "\nWhile attempting fetch from "
                                                "mirror {}:\n{}",
                                                *preferred_mirror,
                                                msg);
                                        });
                                    if (git_repo->FetchViaTmpRepo(
                                            tmp_dir->GetPath(),
                                            *preferred_mirror,
                                            repo_info.branch,
                                            git_bin,
                                            launcher,
                                            wrapped_logger)) {
                                        fetched = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (fetched) {
                            break;
                        }
                        wrapped_logger =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [mirror, &err_messages](auto const& msg,
                                                        bool /*fatal*/) {
                                    err_messages += fmt::format(
                                        "\nWhile attempting fetch from mirror "
                                        "{}:\n{}",
                                        mirror,
                                        msg);
                                });
                        if (git_repo->FetchViaTmpRepo(tmp_dir->GetPath(),
                                                      mirror,
                                                      repo_info.branch,
                                                      git_bin,
                                                      launcher,
                                                      wrapped_logger)) {
                            fetched = true;
                            break;
                        }
                    }
                }
            }
        }
        if (not fetched) {
            // log fetch failure details separately to reduce verbosity
            (*logger)(
                fmt::format("While fetching via tmp repo:{}", err_messages),
                /*fatal=*/false);
            (*logger)("Failed to fetch from provided remotes", /*fatal=*/true);
            return;
        }
        // setup wrapped logger
        wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
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
            (*logger)(fmt::format("Could not fetch commit {} from branch "
                                  "{} for remote {}",
                                  repo_info.hash,
                                  repo_info.branch,
                                  fetch_repo),
                      /*fatal=*/true);
            return;
        }
        // keep tag
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
            [git_cas, repo_info, repo_root, fetch_absent, ws_setter, logger](
                auto const& values) {
                GitOpValue op_result = *values[0];
                // check flag
                if (not op_result.result) {
                    (*logger)("Keep tag failed",
                              /*fatal=*/true);
                    return;
                }
                // ensure commit exists, and fetch if needed
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
                        (*logger)(fmt::format("While getting subtree "
                                              "from commit:\n{}",
                                              msg),
                                  fatal);
                    });
                // get tree id and return workspace root
                auto subtree = git_repo->GetSubtreeFromCommit(
                    repo_info.hash, repo_info.subdir, wrapped_logger);
                if (not subtree) {
                    return;
                }
                // set the workspace root
                JustMRProgress::Instance().TaskTracker().Stop(repo_info.origin);
                auto root = nlohmann::json::array(
                    {repo_info.ignore_special
                         ? FileRoot::kGitTreeIgnoreSpecialMarker
                         : FileRoot::kGitTreeMarker,
                     *subtree});
                if (fetch_absent or not repo_info.absent) {
                    root.emplace_back(repo_root);
                }
                (*ws_setter)(std::pair(std::move(root), false));
            },
            [logger, target_path = repo_root](auto const& msg, bool fatal) {
                (*logger)(fmt::format("While running critical Git op "
                                      "KEEP_TAG for target {}:\n{}",
                                      target_path.string(),
                                      msg),
                          fatal);
            });
    }
    else {
        JustMRProgress::Instance().TaskTracker().Start(repo_info.origin);
        // setup wrapped logger
        auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
            [logger](auto const& msg, bool fatal) {
                (*logger)(
                    fmt::format("While getting subtree from commit:\n{}", msg),
                    fatal);
            });
        // get tree id and return workspace root
        auto subtree = git_repo->GetSubtreeFromCommit(
            repo_info.hash, repo_info.subdir, wrapped_logger);
        if (not subtree) {
            return;
        }
        // set the workspace root
        auto root = nlohmann::json::array(
            {repo_info.ignore_special ? FileRoot::kGitTreeIgnoreSpecialMarker
                                      : FileRoot::kGitTreeMarker,
             *subtree});
        if (fetch_absent or not repo_info.absent) {
            root.emplace_back(repo_root);
        }
        (*ws_setter)(std::pair(std::move(root), true));
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
    bool serve_api_exists,
    IExecutionApi* local_api,
    IExecutionApi* remote_api,
    bool fetch_absent,
    std::size_t jobs) -> CommitGitMap {
    auto commit_to_git = [critical_git_op_map,
                          import_to_git_map,
                          just_mr_paths,
                          additional_mirrors,
                          git_bin,
                          launcher,
                          serve_api_exists,
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
        if (GitURLIsPath(fetch_repo)) {
            fetch_repo = std::filesystem::absolute(fetch_repo).string();
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
             serve_api_exists,
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
                             serve_api_exists,
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
