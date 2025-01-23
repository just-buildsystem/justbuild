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

#include "src/other_tools/root_maps/fpath_git_map.hpp"

#include <memory>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/other_tools/git_operations/git_ops_types.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {

/// \brief Does the serve endpoint checks and sets the workspace root.
/// It guarantees the logger is called exactly once with fatal on failure, and
/// the setter on success.
void CheckServeAndSetRoot(std::string const& tree_id,
                          std::string const& repo_root,
                          bool absent,
                          ServeApi const* serve,
                          FilePathGitMap::SetterPtr const& ws_setter,
                          FilePathGitMap::LoggerPtr const& logger) {
    // if serve endpoint is given, try to ensure it has this tree available to
    // be able to build against it. If root is not absent, do not fail if we
    // don't have a suitable remote endpoint, but warn user nonetheless.
    if (serve != nullptr) {
        auto const has_tree = serve->CheckRootTree(tree_id);
        if (not has_tree) {
            (*logger)(fmt::format("Checking that the serve endpoint knows tree "
                                  "{} failed.",
                                  tree_id),
                      /*fatal=*/true);
            return;
        }
        if (not *has_tree) {
            auto digest =
                ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                              tree_id,
                                              /*size_unknown=*/0,
                                              /*is_tree=*/true);
            if (not digest) {
                (*logger)(std::move(digest).error(), /*fatal=*/true);
                return;
            }

            auto uploaded = serve->UploadTree(*digest, repo_root);
            // only enforce root setup on the serve endpoint if root is absent
            if (not uploaded.has_value()) {
                bool const fatal = absent or not uploaded.error().IsSyncError();
                (*logger)(std::move(uploaded).error().Message(), fatal);
                if (fatal) {
                    return;
                }
            }
        }
    }
    else {
        if (absent) {
            // give warning
            (*logger)(fmt::format("Workspace root {} marked absent but no "
                                  "suitable serve endpoint provided.",
                                  tree_id),
                      /*fatal=*/false);
        }
    }
    // set the workspace root
    auto root = nlohmann::json::array({FileRoot::kGitTreeMarker, tree_id});
    if (not absent) {
        root.emplace_back(repo_root);
    }
    (*ws_setter)(std::move(root));
}

void ResolveFilePathTree(
    std::string const& repo_root,
    std::string const& target_path,
    std::string const& tree_hash,
    std::optional<PragmaSpecial> const& pragma_special,
    GitCASPtr const& source_cas,
    GitCASPtr const& target_cas,
    bool absent,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    gsl::not_null<TaskSystem*> const& ts,
    FilePathGitMap::SetterPtr const& ws_setter,
    FilePathGitMap::LoggerPtr const& logger) {
    if (pragma_special) {
        // get the resolved tree
        auto tree_id_file = StorageUtils::GetResolvedTreeIDFile(
            *native_storage_config, tree_hash, *pragma_special);
        if (FileSystemManager::Exists(tree_id_file)) {
            // read resolved tree id
            auto resolved_tree_id = FileSystemManager::ReadFile(tree_id_file);
            if (not resolved_tree_id) {
                (*logger)(
                    fmt::format("Failed to read resolved tree id from file {}",
                                tree_id_file.string()),
                    /*fatal=*/true);
                return;
            }
            // if serve endpoint is given, try to ensure it has this tree
            // available to be able to build against it; the tree is resolved,
            // so it is in our Git cache
            CheckServeAndSetRoot(*resolved_tree_id,
                                 native_storage_config->GitRoot().string(),
                                 absent,
                                 serve,
                                 ws_setter,
                                 logger);
        }
        else {
            // resolve tree
            resolve_symlinks_map->ConsumeAfterKeysReady(
                ts,
                {GitObjectToResolve(tree_hash,
                                    ".",
                                    *pragma_special,
                                    /*known_info=*/std::nullopt,
                                    source_cas,
                                    target_cas)},
                [critical_git_op_map,
                 tree_hash,
                 tree_id_file,
                 absent,
                 serve,
                 native_storage_config,
                 ts,
                 ws_setter,
                 logger](auto const& hashes) {
                    auto const& resolved_tree_id = hashes[0]->id;
                    // keep tree alive in Git cache via a tagged commit
                    GitOpKey op_key = {
                        .params =
                            {
                                native_storage_config
                                    ->GitRoot(),              // target_path
                                resolved_tree_id,             // git_hash
                                "Keep referenced tree alive"  // message
                            },
                        .op_type = GitOpType::KEEP_TREE};
                    critical_git_op_map->ConsumeAfterKeysReady(
                        ts,
                        {std::move(op_key)},
                        [resolved_tree_id,
                         tree_id_file,
                         absent,
                         serve,
                         native_storage_config,
                         ws_setter,
                         logger](auto const& values) {
                            GitOpValue op_result = *values[0];
                            // check flag
                            if (not op_result.result) {
                                (*logger)("Keep tree failed",
                                          /*fatal=*/true);
                                return;
                            }
                            // cache the resolved tree in the CAS map
                            if (not StorageUtils::WriteTreeIDFile(
                                    tree_id_file, resolved_tree_id)) {
                                (*logger)(
                                    fmt::format("Failed to write resolved tree "
                                                "id to file {}",
                                                tree_id_file.string()),
                                    /*fatal=*/true);
                                return;
                            }
                            // if serve endpoint is given, try to ensure it has
                            // this tree available to be able to build against
                            // it; the resolved tree is in the Git cache
                            CheckServeAndSetRoot(
                                resolved_tree_id,
                                native_storage_config->GitRoot().string(),
                                absent,
                                serve,
                                ws_setter,
                                logger);
                        },
                        [logger,
                         target_path = native_storage_config->GitRoot()](
                            auto const& msg, bool fatal) {
                            (*logger)(
                                fmt::format("While running critical Git op "
                                            "KEEP_TREE for target {}:\n{}",
                                            target_path.string(),
                                            msg),
                                fatal);
                        });
                },
                [logger, target_path](auto const& msg, bool fatal) {
                    (*logger)(fmt::format(
                                  "While resolving symlinks for target {}:\n{}",
                                  target_path,
                                  msg),
                              fatal);
                });
        }
    }
    else {
        // tree needs no further processing;
        // if serve endpoint is given, try to ensure it has this tree available
        // to be able to build against it
        CheckServeAndSetRoot(
            tree_hash, repo_root, absent, serve, ws_setter, logger);
    }
}

}  // namespace

auto CreateFilePathGitMap(
    std::optional<std::string> const& current_subcmd,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    std::size_t jobs,
    std::string const& multi_repo_tool_name,
    std::string const& build_tool_name) -> FilePathGitMap {
    auto dir_to_git = [current_subcmd,
                       critical_git_op_map,
                       import_to_git_map,
                       resolve_symlinks_map,
                       serve,
                       native_storage_config,
                       multi_repo_tool_name,
                       build_tool_name](auto ts,
                                        auto setter,
                                        auto logger,
                                        auto /*unused*/,
                                        auto const& key) {
        // setup wrapped logger
        auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
            [logger](auto const& msg, bool fatal) {
                (*logger)(
                    fmt::format("While getting repo root from path:\n{}", msg),
                    fatal);
            });
        // check if path is a part of a git repo
        auto repo_root = GitRepoRemote::GetRepoRootFromPath(
            key.fpath, wrapped_logger);  // static function
        if (not repo_root) {
            return;
        }
        if (not repo_root->empty()) {  // if repo root found
            // get head commit
            GitOpKey op_key = {.params =
                                   {
                                       *repo_root,  // target_path
                                       "",          // git_hash
                                   },
                               .op_type = GitOpType::GET_HEAD_ID};
            critical_git_op_map->ConsumeAfterKeysReady(
                ts,
                {std::move(op_key)},
                [fpath = key.fpath,
                 pragma_special = key.pragma_special,
                 absent = key.absent,
                 repo_root = std::move(*repo_root),
                 critical_git_op_map,
                 resolve_symlinks_map,
                 serve,
                 native_storage_config,
                 ts,
                 setter,
                 logger](auto const& values) {
                    GitOpValue op_result = *values[0];
                    // check flag
                    if (not op_result.result) {
                        (*logger)("Get Git head id failed",
                                  /*fatal=*/true);
                        return;
                    }
                    auto git_repo = GitRepoRemote::Open(
                        op_result.git_cas);  // link fake repo to odb
                    if (not git_repo) {
                        (*logger)(fmt::format("Could not open repository {}",
                                              repo_root.string()),
                                  /*fatal=*/true);
                        return;
                    }
                    // setup wrapped logger
                    auto wrapped_logger =
                        std::make_shared<AsyncMapConsumerLogger>(
                            [logger](auto const& msg, bool fatal) {
                                (*logger)(
                                    fmt::format("While getting subtree from "
                                                "path:\n{}",
                                                msg),
                                    fatal);
                            });
                    // get tree id
                    auto tree_hash = git_repo->GetSubtreeFromPath(
                        fpath, *op_result.result, wrapped_logger);
                    if (not tree_hash) {
                        return;
                    }
                    // resolve tree and set workspace root; tree gets resolved
                    // from source repo into the Git cache, which we first need
                    // to ensure is initialized
                    GitOpKey op_key = {.params =
                                           {
                                               native_storage_config
                                                   ->GitRoot(),  // target_path
                                               "",               // git_hash
                                               std::nullopt,     // message
                                               std::nullopt,     // source_path
                                               true              // init_bare
                                           },
                                       .op_type = GitOpType::ENSURE_INIT};
                    critical_git_op_map->ConsumeAfterKeysReady(
                        ts,
                        {std::move(op_key)},
                        [repo_root,
                         fpath,
                         tree_hash,
                         pragma_special,
                         source_cas = op_result.git_cas,
                         absent,
                         critical_git_op_map,
                         resolve_symlinks_map,
                         serve,
                         native_storage_config,
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
                            ResolveFilePathTree(
                                repo_root.string(),
                                fpath.string(),
                                *tree_hash,
                                pragma_special,
                                source_cas,
                                op_result.git_cas, /*just_git_cas*/
                                absent,
                                critical_git_op_map,
                                resolve_symlinks_map,
                                serve,
                                native_storage_config,
                                ts,
                                setter,
                                logger);
                        },
                        [logger,
                         target_path = native_storage_config->GitRoot()](
                            auto const& msg, bool fatal) {
                            (*logger)(
                                fmt::format("While running critical Git op "
                                            "ENSURE_INIT for target {}:\n{}",
                                            target_path.string(),
                                            msg),
                                fatal);
                        });
                },
                [logger, target_path = *repo_root](auto const& msg,
                                                   bool fatal) {
                    (*logger)(fmt::format("While running critical Git op "
                                          "GET_HEAD_ID for target {}:\n{}",
                                          target_path.string(),
                                          msg),
                              fatal);
                });
        }
        else {
            // warn if import to git is inefficient
            if (current_subcmd) {
                (*logger)(fmt::format("Inefficient Git import of file "
                                      "path \'{}\'.\nPlease consider using "
                                      "\'{} setup\' and \'{} {}\' "
                                      "separately to cache the output.",
                                      key.fpath.string(),
                                      multi_repo_tool_name,
                                      build_tool_name,
                                      *current_subcmd),
                          /*fatal=*/false);
            }
            // it's not a git repo, so import it to git cache
            auto tmp_dir = native_storage_config->CreateTypedTmpDir("file");
            if (not tmp_dir) {
                (*logger)("Failed to create import-to-git tmp directory!",
                          /*fatal=*/true);
                return;
            }
            // copy folder content to tmp dir
            if (not FileSystemManager::CopyDirectoryImpl(
                    key.fpath, tmp_dir->GetPath(), /*recursively=*/true)) {
                (*logger)(
                    fmt::format("Failed to copy content from directory {}",
                                key.fpath.string()),
                    /*fatal=*/true);
                return;
            }
            // do import to git
            CommitInfo c_info{tmp_dir->GetPath(), "file", key.fpath};
            import_to_git_map->ConsumeAfterKeysReady(
                ts,
                {std::move(c_info)},
                // tmp_dir passed, to ensure folder is not removed until import
                // to git is done
                [tmp_dir,
                 fpath = key.fpath,
                 pragma_special = key.pragma_special,
                 absent = key.absent,
                 critical_git_op_map,
                 resolve_symlinks_map,
                 serve,
                 native_storage_config,
                 ts,
                 setter,
                 logger](auto const& values) {
                    // check for errors
                    if (not values[0]->second) {
                        (*logger)("Importing to git failed",
                                  /*fatal=*/true);
                        return;
                    }
                    // we only need the tree
                    std::string tree = values[0]->first;
                    // resolve tree and set workspace root;
                    // we work on the Git CAS directly
                    ResolveFilePathTree(
                        native_storage_config->GitRoot().string(),
                        fpath.string(),
                        tree,
                        pragma_special,
                        values[0]->second, /*source_cas*/
                        values[0]->second, /*target_cas*/
                        absent,
                        critical_git_op_map,
                        resolve_symlinks_map,
                        serve,
                        native_storage_config,
                        ts,
                        setter,
                        logger);
                },
                [logger, target_path = key.fpath](auto const& msg, bool fatal) {
                    (*logger)(
                        fmt::format("While importing target {} to git:\n{}",
                                    target_path.string(),
                                    msg),
                        fatal);
                });
        }
    };
    return AsyncMapConsumer<FpathInfo, nlohmann::json>(dir_to_git, jobs);
}
