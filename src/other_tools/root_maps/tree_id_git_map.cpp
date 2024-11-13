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
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_info.hpp"
#include "src/buildtool/execution_api/serve/mr_git_api.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/root_maps/root_utils.hpp"

namespace {

/// \brief Guarantees it terminates by either calling the setter or calling the
/// logger with fatal.
void UploadToServeAndSetRoot(
    ServeApi const& serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    Storage const* compat_storage,
    std::string const& tree_id,
    ArtifactDigest const& digest,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const& remote_api,
    bool ignore_special,
    TreeIdGitMap::SetterPtr const& setter,
    TreeIdGitMap::LoggerPtr const& logger) {
    // upload to remote CAS
    auto repo_config = RepositoryConfig{};
    if (repo_config.SetGitCAS(native_storage_config->GitRoot())) {
        auto git_api =
            MRGitApi{&repo_config,
                     native_storage_config,
                     compat_storage_config,
                     compat_storage,
                     compat_storage_config != nullptr ? &*local_api : nullptr};
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
                              native_storage_config->GitRoot().string()),
                  /*fatal=*/true);
        return;
    }
    // tell serve to set up the root from the remote CAS tree;
    // upload can be skipped
    if (EnsureAbsentRootOnServe(serve,
                                tree_id,
                                /*repo_path=*/"",
                                native_storage_config,
                                /*compat_storage_config=*/nullptr,
                                /*compat_storage=*/nullptr,
                                /*local_api=*/nullptr,
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
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    Storage const* compat_storage,
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
    auto tmp_dir =
        native_storage_config->CreateTypedTmpDir("fetch-remote-git-tree");
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
         native_storage_config,
         compat_storage_config,
         compat_storage,
         tmp_dir,  // keep tmp_dir alive
         tree_id,
         digest,
         local_api,
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
                                    native_storage_config,
                                    compat_storage_config,
                                    compat_storage,
                                    tree_id,
                                    digest,
                                    local_api,
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
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    Storage const* compat_storage,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    std::size_t jobs) -> TreeIdGitMap {
    auto tree_to_git = [git_tree_fetch_map,
                        critical_git_op_map,
                        import_to_git_map,
                        fetch_absent,
                        serve,
                        native_storage_config,
                        compat_storage_config,
                        compat_storage,
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
                auto has_tree = CheckServeHasAbsentRoot(
                    *serve, key.tree_info.tree_hash.Hash(), logger);
                if (not has_tree) {
                    return;
                }
                if (*has_tree) {
                    // set workspace root as absent
                    auto root = nlohmann::json::array(
                        {key.ignore_special
                             ? FileRoot::kGitTreeIgnoreSpecialMarker
                             : FileRoot::kGitTreeMarker,
                         key.tree_info.tree_hash.Hash()});
                    (*setter)(
                        std::pair(std::move(root), /*is_cache_hit=*/false));
                    return;
                }
                // we cannot continue without a suitable remote set up
                if (remote_api == nullptr) {
                    (*logger)(
                        fmt::format("Cannot create workspace root {} as absent "
                                    "for the provided serve endpoint.",
                                    key.tree_info.tree_hash.Hash()),
                        /*fatal=*/true);
                    return;
                }
                // check if tree in already in remote CAS
                auto const digest = ArtifactDigest{key.tree_info.tree_hash, 0};
                if (remote_api->IsAvailable({digest})) {
                    // tell serve to set up the root from the remote CAS tree;
                    // upload can be skipped
                    if (EnsureAbsentRootOnServe(
                            *serve,
                            key.tree_info.tree_hash.Hash(),
                            /*repo_path=*/"",
                            native_storage_config,
                            /*compat_storage_config=*/nullptr,
                            /*compat_storage=*/nullptr,
                            /*local_api=*/nullptr,
                            /*remote_api=*/nullptr,
                            logger,
                            /*no_sync_is_fatal=*/true)) {
                        // set workspace root as absent
                        auto root = nlohmann::json::array(
                            {key.ignore_special
                                 ? FileRoot::kGitTreeIgnoreSpecialMarker
                                 : FileRoot::kGitTreeMarker,
                             key.tree_info.tree_hash.Hash()});
                        (*setter)(
                            std::pair(std::move(root), /*is_cache_hit=*/false));
                        return;
                    }
                    (*logger)(
                        fmt::format("Serve endpoint failed to create workspace "
                                    "root {} that locally was marked absent.",
                                    key.tree_info.tree_hash.Hash()),
                        /*fatal=*/true);
                    return;
                }
                // check if tree is in Git cache;
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
                    [serve,
                     native_storage_config,
                     compat_storage_config,
                     compat_storage,
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
                                fmt::format(
                                    "Could not open repository {}",
                                    native_storage_config->GitRoot().string()),
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
                            key.tree_info.tree_hash.Hash(), wrapped_logger);
                        if (not tree_found) {
                            // errors encountered
                            return;
                        }
                        if (*tree_found) {
                            // upload tree from Git cache to remote CAS and tell
                            // serve to set up the root from the remote CAS
                            // tree, then set root as absent
                            UploadToServeAndSetRoot(
                                *serve,
                                native_storage_config,
                                compat_storage_config,
                                compat_storage,
                                key.tree_info.tree_hash.Hash(),
                                digest,
                                local_api,
                                *remote_api,
                                key.ignore_special,
                                setter,
                                logger);
                            // done!
                            return;
                        }
                        // check if tree is known to local CAS
                        if (local_api->IsAvailable(digest)) {
                            // Move tree locally from CAS to Git cache, then
                            // continue processing it by UploadToServeAndSetRoot
                            MoveCASTreeToGitAndProcess(
                                *serve,
                                native_storage_config,
                                compat_storage_config,
                                compat_storage,
                                key.tree_info.tree_hash.Hash(),
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
                                              key.tree_info.tree_hash.Hash()),
                                  /*fatal=*/true);
                    },
                    [logger, target_path = native_storage_config->GitRoot()](
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
                                  key.tree_info.tree_hash.Hash()),
                      /*fatal=*/false);
            // set workspace root as absent
            auto root = nlohmann::json::array(
                {key.ignore_special ? FileRoot::kGitTreeIgnoreSpecialMarker
                                    : FileRoot::kGitTreeMarker,
                 key.tree_info.tree_hash.Hash()});
            (*setter)(std::pair(std::move(root), false));
            return;
        }
        // if root is not absent, proceed with usual fetch logic: check locally,
        // check serve endpoint, check remote-execution endpoint, and lastly
        // default to network
        git_tree_fetch_map->ConsumeAfterKeysReady(
            ts,
            {key.tree_info},
            [native_storage_config, key, setter](auto const& values) {
                // tree is now in Git cache;
                // get cache hit info
                auto is_cache_hit = *values[0];
                // set the workspace root as present
                (*setter)(
                    std::pair(nlohmann::json::array(
                                  {key.ignore_special
                                       ? FileRoot::kGitTreeIgnoreSpecialMarker
                                       : FileRoot::kGitTreeMarker,
                                   key.tree_info.tree_hash.Hash(),
                                   native_storage_config->GitRoot().string()}),
                              is_cache_hit));
            },
            [logger, hash = key.tree_info.tree_hash.Hash()](auto const& msg,
                                                            bool fatal) {
                (*logger)(fmt::format(
                              "While ensuring git-tree {} is in Git cache:\n{}",
                              hash,
                              msg),
                          fatal);
            });
    };
    return AsyncMapConsumer<TreeIdInfo, std::pair<nlohmann::json, bool>>(
        tree_to_git, jobs);
}
