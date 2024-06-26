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
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/git/git_api.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/root_maps/root_utils.hpp"

namespace {

/// \brief Guarantees it terminates by either calling the setter or calling the
/// logger with fatal.
void UploadToServeAndSetRoot(ServeApi const& serve,
                             std::string const& tree_id,
                             ArtifactDigest const& digest,
                             IExecutionApi const& remote_api,
                             bool ignore_special,
                             TreeIdGitMap::SetterPtr const& setter,
                             TreeIdGitMap::LoggerPtr const& logger) {
    // upload to remote CAS
    auto repo_config = RepositoryConfig{};
    if (repo_config.SetGitCAS(StorageConfig::GitRoot())) {
        auto git_api = GitApi{&repo_config};
        if (not git_api.RetrieveToCas(
                {Artifact::ObjectInfo{.digest = digest,
                                      .type = ObjectType::Tree}},
                remote_api)) {
            (*logger)(fmt::format("Failed to sync tree {} from local Git cache "
                                  "to remote CAS",
                                  tree_id),
                      /*fatal=*/true);
            return;
        }
    }
    else {
        (*logger)(fmt::format("Failed to SetGitCAS at {}",
                              StorageConfig::GitRoot().string()),
                  /*fatal=*/true);
        return;
    }
    // tell serve to set up the root from the remote CAS tree;
    // upload can be skipped
    if (EnsureAbsentRootOnServe(serve,
                                tree_id,
                                /*repo_path=*/"",
                                /*remote_api=*/nullptr,
                                logger,
                                /*no_sync_is_fatal=*/true)) {
        // set workspace root as absent
        auto root = nlohmann::json::array(
            {ignore_special ? FileRoot::kGitTreeIgnoreSpecialMarker
                            : FileRoot::kGitTreeMarker,
             tree_id});
        (*setter)(std::pair(std::move(root), /*is_cache_hit=*/false));
        return;
    }
}

/// \brief Guarantees it terminates by either calling the setter or calling the
/// logger with fatal.
void MoveCASTreeToGitAndProcess(
    ServeApi const& serve,
    std::string const& tree_id,
    ArtifactDigest const& digest,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<IExecutionApi const*> const& local_api,
    gsl::not_null<IExecutionApi const*> const& remote_api,
    bool ignore_special,
    gsl::not_null<TaskSystem*> const& ts,
    TreeIdGitMap::SetterPtr const& setter,
    TreeIdGitMap::LoggerPtr const& logger) {
    // Move tree from CAS to local Git storage
    auto tmp_dir = StorageConfig::CreateTypedTmpDir("fetch-remote-git-tree");
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
        [&serve,
         tmp_dir,  // keep tmp_dir alive
         tree_id,
         digest,
         remote_api,
         ignore_special,
         setter,
         logger](auto const& values) {
            if (not values[0]->second) {
                (*logger)("Importing to git failed",
                          /*fatal=*/true);
                return;
            }
            // upload tree from Git cache to remote CAS and tell serve to set up
            // the root from the remote CAS tree; set root as absent on success
            UploadToServeAndSetRoot(serve,
                                    tree_id,
                                    digest,
                                    *remote_api,
                                    ignore_special,
                                    setter,
                                    logger);
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

auto CreateTreeIdGitMap(
    gsl::not_null<GitTreeFetchMap*> const& git_tree_fetch_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    bool fetch_absent,
    ServeApi const* serve,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    std::size_t jobs) -> TreeIdGitMap {
    auto tree_to_git = [git_tree_fetch_map,
                        critical_git_op_map,
                        import_to_git_map,
                        fetch_absent,
                        serve,
                        local_api,
                        remote_api](auto ts,
                                    auto setter,
                                    auto logger,
                                    auto /*unused*/,
                                    auto const& key) {
        // if root is actually absent, check if serve endpoint knows the tree
        // for building against it and only set the workspace root if tree is
        // found on the serve endpoint or it can be made available to it;
        // otherwise, error out
        if (key.absent and not fetch_absent) {
            if (serve != nullptr) {
                // check serve endpoint
                auto has_tree =
                    CheckServeHasAbsentRoot(*serve, key.tree_info.hash, logger);
                if (not has_tree) {
                    return;
                }
                if (*has_tree) {
                    // set workspace root as absent
                    auto root = nlohmann::json::array(
                        {key.ignore_special
                             ? FileRoot::kGitTreeIgnoreSpecialMarker
                             : FileRoot::kGitTreeMarker,
                         key.tree_info.hash});
                    (*setter)(
                        std::pair(std::move(root), /*is_cache_hit=*/false));
                    return;
                }
                // we cannot continue without a suitable remote set up
                if (remote_api == nullptr) {
                    (*logger)(
                        fmt::format("Cannot create workspace root {} as absent "
                                    "for the provided serve endpoint.",
                                    key.tree_info.hash),
                        /*fatal=*/true);
                    return;
                }
                // check if tree in already in remote CAS
                auto digest =
                    ArtifactDigest{key.tree_info.hash, 0, /*is_tree=*/true};
                if (remote_api->IsAvailable({digest})) {
                    // tell serve to set up the root from the remote CAS tree;
                    // upload can be skipped
                    if (EnsureAbsentRootOnServe(*serve,
                                                key.tree_info.hash,
                                                /*repo_path=*/"",
                                                /*remote_api=*/nullptr,
                                                logger,
                                                /*no_sync_is_fatal=*/true)) {
                        // set workspace root as absent
                        auto root = nlohmann::json::array(
                            {key.ignore_special
                                 ? FileRoot::kGitTreeIgnoreSpecialMarker
                                 : FileRoot::kGitTreeMarker,
                             key.tree_info.hash});
                        (*setter)(
                            std::pair(std::move(root), /*is_cache_hit=*/false));
                        return;
                    }
                    (*logger)(
                        fmt::format("Serve endpoint failed to create workspace "
                                    "root {} that locally was marked absent.",
                                    key.tree_info.hash),
                        /*fatal=*/true);
                    return;
                }
                // check if tree is in Git cache;
                // ensure Git cache exists
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
                    [serve,
                     digest,
                     import_to_git_map,
                     local_api,
                     remote_api,
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
                        // Open fake tmp repo to check if tree is known to Git
                        // cache
                        auto git_repo = GitRepoRemote::Open(
                            op_result.git_cas);  // link fake repo to odb
                        if (not git_repo) {
                            (*logger)(
                                fmt::format("Could not open repository {}",
                                            StorageConfig::GitRoot().string()),
                                /*fatal=*/true);
                            return;
                        }
                        // setup wrapped logger
                        auto wrapped_logger =
                            std::make_shared<AsyncMapConsumerLogger>(
                                [logger](auto const& msg, bool fatal) {
                                    (*logger)(
                                        fmt::format("While checking tree "
                                                    "exists in Git cache:\n{}",
                                                    msg),
                                        fatal);
                                });
                        // check if the desired tree ID is in Git cache
                        auto tree_found = git_repo->CheckTreeExists(
                            key.tree_info.hash, wrapped_logger);
                        if (not tree_found) {
                            // errors encountered
                            return;
                        }
                        if (*tree_found) {
                            // upload tree from Git cache to remote CAS and tell
                            // serve to set up the root from the remote CAS
                            // tree, then set root as absent
                            UploadToServeAndSetRoot(*serve,
                                                    key.tree_info.hash,
                                                    digest,
                                                    *remote_api,
                                                    key.ignore_special,
                                                    setter,
                                                    logger);
                            // done!
                            return;
                        }
                        // check if tree is known to local CAS
                        auto const& cas = Storage::Instance().CAS();
                        if (auto path = cas.TreePath(digest)) {
                            // Move tree locally from CAS to Git cache, then
                            // continue processing it by UploadToServeAndSetRoot
                            MoveCASTreeToGitAndProcess(*serve,
                                                       key.tree_info.hash,
                                                       digest,
                                                       import_to_git_map,
                                                       local_api,
                                                       remote_api,
                                                       key.ignore_special,
                                                       ts,
                                                       setter,
                                                       logger);
                            // done!
                            return;
                        }
                        // tree is not know locally, so we cannot
                        // provide it to the serve endpoint and thus we
                        // cannot create the absent root
                        (*logger)(fmt::format("Cannot create workspace root "
                                              "{} as absent for the provided "
                                              "serve endpoint.",
                                              key.tree_info.hash),
                                  /*fatal=*/true);
                    },
                    [logger, target_path = StorageConfig::GitRoot()](
                        auto const& msg, bool fatal) {
                        (*logger)(
                            fmt::format("While running critical Git op "
                                        "ENSURE_INIT bare for target {}:\n{}",
                                        target_path.string(),
                                        msg),
                            fatal);
                    });
                // done!
                return;
            }
            // give warning that serve endpoint is missing
            (*logger)(fmt::format("Workspace root {} marked absent but no "
                                  "suitable serve endpoint provided.",
                                  key.tree_info.hash),
                      /*fatal=*/false);
            // set workspace root as absent
            auto root = nlohmann::json::array(
                {key.ignore_special ? FileRoot::kGitTreeIgnoreSpecialMarker
                                    : FileRoot::kGitTreeMarker,
                 key.tree_info.hash});
            (*setter)(std::pair(std::move(root), false));
            return;
        }
        // if root is not absent, proceed with usual fetch logic: check locally,
        // check serve endpoint, check remote-execution endpoint, and lastly
        // default to network
        git_tree_fetch_map->ConsumeAfterKeysReady(
            ts,
            {key.tree_info},
            [key, setter](auto const& values) {
                // tree is now in Git cache;
                // get cache hit info
                auto is_cache_hit = *values[0];
                // set the workspace root as present
                (*setter)(
                    std::pair(nlohmann::json::array(
                                  {key.ignore_special
                                       ? FileRoot::kGitTreeIgnoreSpecialMarker
                                       : FileRoot::kGitTreeMarker,
                                   key.tree_info.hash,
                                   StorageConfig::GitRoot().string()}),
                              is_cache_hit));
            },
            [logger, tree_id = key.tree_info.hash](auto const& msg,
                                                   bool fatal) {
                (*logger)(fmt::format(
                              "While ensuring git-tree {} is in Git cache:\n{}",
                              tree_id,
                              msg),
                          fatal);
            });
    };
    return AsyncMapConsumer<TreeIdInfo, std::pair<nlohmann::json, bool>>(
        tree_to_git, jobs);
}
