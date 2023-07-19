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

#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {

void ResolveFilePathTree(

    std::string const& repo_root,
    std::string const& target_path,
    std::string const& tree_hash,
    std::optional<PragmaSpecial> const& pragma_special,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    FilePathGitMap::SetterPtr const& ws_setter,
    FilePathGitMap::LoggerPtr const& logger) {
    if (pragma_special) {
        // get the resolved tree
        auto tree_id_file =
            JustMR::Utils::GetResolvedTreeIDFile(tree_hash, *pragma_special);
        if (FileSystemManager::Exists(tree_id_file)) {
            // read resolved tree id
            auto resolved_tree_id = FileSystemManager::ReadFile(tree_id_file);
            if (not resolved_tree_id) {
                (*logger)(fmt::format("Failed to read resolved "
                                      "tree id from file {}",
                                      tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            // set the workspace root
            (*ws_setter)(nlohmann::json::array(
                {FileRoot::kGitTreeMarker, *resolved_tree_id, repo_root}));
        }
        else {
            // resolve tree
            resolve_symlinks_map->ConsumeAfterKeysReady(
                ts,
                {GitObjectToResolve(tree_hash,
                                    ".",
                                    *pragma_special,
                                    /*known_info=*/std::nullopt)},
                [resolve_symlinks_map,
                 tree_hash,
                 repo_root,
                 tree_id_file,
                 ws_setter,
                 logger](auto const& hashes) {
                    if (not hashes[0]) {
                        // check for cycles
                        auto error = DetectAndReportCycle(*resolve_symlinks_map,
                                                          tree_hash);
                        if (error) {
                            (*logger)(fmt::format("Failed to resolve symlinks "
                                                  "in tree {}:\n{}",
                                                  tree_hash,
                                                  *error),
                                      /*fatal=*/true);
                            return;
                        }
                        (*logger)(fmt::format("Unknown error in resolving "
                                              "symlinks in tree {}",
                                              tree_hash),
                                  /*fatal=*/true);
                        return;
                    }
                    auto const& resolved_tree = *hashes[0];
                    // cache the resolved tree in the CAS map
                    if (not JustMR::Utils::WriteTreeIDFile(tree_id_file,
                                                           resolved_tree.id)) {
                        (*logger)(fmt::format("Failed to write resolved tree "
                                              "id to file {}",
                                              tree_id_file.string()),
                                  /*fatal=*/true);
                        return;
                    }
                    // set the workspace root
                    (*ws_setter)(
                        nlohmann::json::array({FileRoot::kGitTreeMarker,
                                               resolved_tree.id,
                                               repo_root}));
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
        // set the workspace root as-is
        (*ws_setter)(nlohmann::json::array(
            {FileRoot::kGitTreeMarker, tree_hash, repo_root}));
    }
}

}  // namespace

auto CreateFilePathGitMap(
    std::optional<std::string> const& current_subcmd,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    std::size_t jobs) -> FilePathGitMap {
    auto dir_to_git = [current_subcmd,
                       critical_git_op_map,
                       import_to_git_map,
                       resolve_symlinks_map](auto ts,
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
            auto git_cas = GitCAS::Open(*repo_root);
            if (not git_cas) {
                (*logger)(fmt::format("Could not open object database for "
                                      "repository {}",
                                      repo_root->string()),
                          /*fatal=*/true);
                return;
            }
            auto git_repo =
                GitRepoRemote::Open(git_cas);  // link fake repo to odb
            if (not git_repo) {
                (*logger)(fmt::format("Could not open repository {}",
                                      repo_root->string()),
                          /*fatal=*/true);
                return;
            }
            // get head commit
            GitOpKey op_key = {.params =
                                   {
                                       *repo_root,  // target_path
                                       "",          // git_hash
                                       "",          // branch
                                   },
                               .op_type = GitOpType::GET_HEAD_ID};
            critical_git_op_map->ConsumeAfterKeysReady(
                ts,
                {std::move(op_key)},
                [fpath = key.fpath,
                 pragma_special = key.pragma_special,
                 git_cas = std::move(git_cas),
                 repo_root = std::move(*repo_root),
                 resolve_symlinks_map,
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
                    auto git_repo =
                        GitRepoRemote::Open(git_cas);  // link fake repo to odb
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
                    // resolve tree and set workspace root
                    ResolveFilePathTree(repo_root.string(),
                                        fpath.string(),
                                        *tree_hash,
                                        pragma_special,
                                        resolve_symlinks_map,
                                        ts,
                                        setter,
                                        logger);
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
                (*logger)(fmt::format("Warning: Inefficient Git import of file "
                                      "path \'{}\'.\nPlease consider using "
                                      "\'just-mr setup\' and \'just {}\' "
                                      "separately to cache the output.",
                                      key.fpath.string(),
                                      *current_subcmd),
                          /*fatal=*/false);
            }
            // it's not a git repo, so import it to git cache
            auto tmp_dir = JustMR::Utils::CreateTypedTmpDir("file");
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
                 resolve_symlinks_map,
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
                    // resolve tree and set workspace root
                    ResolveFilePathTree(StorageConfig::GitRoot().string(),
                                        fpath.string(),
                                        tree,
                                        pragma_special,
                                        resolve_symlinks_map,
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
