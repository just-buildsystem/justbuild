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

#include "src/other_tools/root_maps/distdir_git_map.hpp"

#include <algorithm>

#include "fmt/core.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"
#include "src/other_tools/root_maps/root_utils.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {

/// \brief Create links from CAS content to distdir tmp directory
[[nodiscard]] auto LinkToCAS(
    std::shared_ptr<std::unordered_map<std::string, std::string>> const&
        content_list,
    std::filesystem::path const& tmp_dir) noexcept -> bool {
    auto const& cas = Storage::Instance().CAS();
    return std::all_of(content_list->begin(),
                       content_list->end(),
                       [&cas, tmp_dir](auto const& kv) {
                           auto content_path =
                               cas.BlobPath(ArtifactDigest(kv.second, 0, false),
                                            /*is_executable=*/false);
                           if (content_path) {
                               return FileSystemManager::CreateFileHardlink(
                                   *content_path,  // from: cas_path/content_id
                                   tmp_dir / kv.first);  // to: tmp_dir/name
                           }
                           return false;
                       });
}

/// \brief Called once we know we have the content blobs in local CAS in order
/// to do the import-to-git step. Then it also sets the root.
/// It guarantees the logger is called exactly once with fatal on failure, and
/// the setter on success.
void ImportFromCASAndSetRoot(
    DistdirInfo const& key,
    std::filesystem::path const& distdir_tree_id_file,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<TaskSystem*> const& ts,
    DistdirGitMap::SetterPtr const& setter,
    DistdirGitMap::LoggerPtr const& logger) {
    // create the links to CAS
    auto tmp_dir = StorageConfig::CreateTypedTmpDir("distdir");
    if (not tmp_dir) {
        (*logger)(fmt::format("Failed to create tmp path for "
                              "distdir target {}",
                              key.content_id),
                  /*fatal=*/true);
        return;
    }
    // link content from CAS into tmp dir
    if (not LinkToCAS(key.content_list, tmp_dir->GetPath())) {
        (*logger)(fmt::format("Failed to create links to CAS content!",
                              key.content_id),
                  /*fatal=*/true);
        return;
    }
    // do import to git
    CommitInfo c_info{tmp_dir->GetPath(), "distdir", key.content_id};
    import_to_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(c_info)},
        [tmp_dir,  // keep tmp_dir alive
         distdir_tree_id_file,
         setter,
         logger](auto const& values) {
            // check for errors
            if (not values[0]->second) {
                (*logger)("Importing to git failed",
                          /*fatal=*/true);
                return;
            }
            // only the tree is of interest
            std::string distdir_tree_id = values[0]->first;
            // write to tree id file
            if (not StorageUtils::WriteTreeIDFile(distdir_tree_id_file,
                                                  distdir_tree_id)) {
                (*logger)(fmt::format("Failed to write tree id to file {}",
                                      distdir_tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            // set the workspace root as present
            (*setter)(std::pair(
                nlohmann::json::array({FileRoot::kGitTreeMarker,
                                       distdir_tree_id,
                                       StorageConfig::GitRoot().string()}),
                /*is_cache_hit=*/false));
        },
        [logger, target_path = tmp_dir->GetPath()](auto const& msg,
                                                   bool fatal) {
            (*logger)(fmt::format("While importing target {} to git:\n{}",
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}

}  // namespace

auto CreateDistdirGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    bool serve_api_exists,
    gsl::not_null<IExecutionApi*> const& local_api,
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
    std::size_t jobs) -> DistdirGitMap {
    auto distdir_to_git = [content_cas_map,
                           import_to_git_map,
                           critical_git_op_map,
                           serve_api_exists,
                           local_api,
                           remote_api](auto ts,
                                       auto setter,
                                       auto logger,
                                       auto /* unused */,
                                       auto const& key) {
        auto distdir_tree_id_file =
            StorageUtils::GetDistdirTreeIDFile(key.content_id);
        if (FileSystemManager::Exists(distdir_tree_id_file)) {
            // read distdir_tree_id from file tree_id_file
            auto distdir_tree_id =
                FileSystemManager::ReadFile(distdir_tree_id_file);
            if (not distdir_tree_id) {
                (*logger)(fmt::format("Failed to read tree id from file {}",
                                      distdir_tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            // ensure Git cache
            // define Git operation to be done
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
                [distdir_tree_id = *distdir_tree_id,
                 content_id = key.content_id,
                 key,
                 serve_api_exists,
                 remote_api,
                 setter,
                 logger](auto const& values) {
                    GitOpValue op_result = *values[0];
                    // check flag
                    if (not op_result.result) {
                        (*logger)("Git init failed",
                                  /*fatal=*/true);
                        return;
                    }
                    // subdir is "." here, so no need to deal with the Git cache
                    // and we can simply set the workspace root
                    if (key.absent) {
                        if (serve_api_exists) {
                            // check if serve endpoint has this root
                            auto has_tree = CheckServeHasAbsentRoot(
                                distdir_tree_id, logger);
                            if (not has_tree) {
                                return;
                            }
                            if (not *has_tree) {
                                // try to see if serve endpoint has the
                                // information to prepare the root itself
                                auto serve_result =
                                    ServeApi::Instance()
                                        .RetrieveTreeFromDistdir(
                                            key.content_list,
                                            /*sync_tree=*/false);
                                if (std::holds_alternative<std::string>(
                                        serve_result)) {
                                    // if serve has set up the tree, it must
                                    // match what we expect
                                    auto const& served_tree_id =
                                        std::get<std::string>(serve_result);
                                    if (distdir_tree_id != served_tree_id) {
                                        (*logger)(
                                            fmt::format(
                                                "Mismatch in served root tree "
                                                "id:\nexpected {}, but got {}",
                                                distdir_tree_id,
                                                served_tree_id),
                                            /*fatal=*/true);
                                        return;
                                    }
                                }
                                else {
                                    // check if serve failure was due to distdir
                                    // content not being found or it is
                                    // otherwise fatal
                                    auto const& is_fatal =
                                        std::get<bool>(serve_result);
                                    if (is_fatal) {
                                        (*logger)(
                                            fmt::format(
                                                "Serve endpoint failed to set "
                                                "up root from known distdir "
                                                "content {}",
                                                content_id),
                                            /*fatal=*/true);
                                        return;
                                    }
                                    if (not remote_api) {
                                        (*logger)(
                                            fmt::format(
                                                "Missing or incompatible "
                                                "remote-execution endpoint "
                                                "needed to sync workspace root "
                                                "{} with the serve endpoint.",
                                                distdir_tree_id),
                                            /*fatal=*/true);
                                        return;
                                    }
                                    // the tree is known locally, so we upload
                                    // it to remote CAS for the serve endpoint
                                    // to retrieve it and set up the root
                                    if (not EnsureAbsentRootOnServe(
                                            distdir_tree_id,
                                            StorageConfig::GitRoot(),
                                            &(*remote_api.value()),
                                            logger,
                                            true /*no_sync_is_fatal*/)) {
                                        return;
                                    }
                                }
                            }
                        }
                        else {
                            // give warning
                            (*logger)(
                                fmt::format("Workspace root {} marked absent "
                                            "but no serve endpoint provided.",
                                            distdir_tree_id),
                                /*fatal=*/false);
                        }
                        // set root as absent
                        (*setter)(std::pair(
                            nlohmann::json::array(
                                {FileRoot::kGitTreeMarker, distdir_tree_id}),
                            /*is_cache_hit=*/true));
                    }
                    else {
                        // set root as present
                        (*setter)(
                            std::pair(nlohmann::json::array(
                                          {FileRoot::kGitTreeMarker,
                                           distdir_tree_id,
                                           StorageConfig::GitRoot().string()}),
                                      /*is_cache_hit=*/true));
                    }
                },
                [logger, target_path = StorageConfig::GitRoot()](
                    auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While running critical Git op "
                                          "ENSURE_INIT for target {}:\n{}",
                                          target_path.string(),
                                          msg),
                              fatal);
                });
        }
        // if no association file exists
        else {
            // create in-memory Git tree of distdir content to get the tree id
            GitRepo::tree_entries_t entries{};
            entries.reserve(key.content_list->size());
            for (auto const& kv : *key.content_list) {
                // tree_entries_t type expects raw ids
                auto raw_id = FromHexString(kv.second);
                if (not raw_id) {
                    (*logger)(fmt::format("While processing distdir {}: "
                                          "Unexpected failure in conversion to "
                                          "raw id of distfile content {}",
                                          key.content_id,
                                          kv.second),
                              /*is_fatal=*/true);
                    return;
                }
                entries[*raw_id].emplace_back(kv.first, ObjectType::File);
            }
            auto tree = GitRepo::CreateShallowTree(entries);
            if (not tree) {
                (*logger)(fmt::format("Failed to construct in-memory tree for "
                                      "distdir content {}",
                                      key.content_id),
                          /*is_fatal=*/true);
                return;
            }
            // get hash from raw_id
            auto tree_id = ToHexString(tree->first);
            // get digest object
            auto digest = ArtifactDigest{tree_id, 0, /*is_tree=*/true};

            // use this knowledge of the resulting tree identifier to try to set
            // up the absent root without actually checking the local status of
            // each content blob individually
            if (key.absent) {
                if (serve_api_exists) {
                    // first check if serve endpoint has tree
                    auto has_tree = CheckServeHasAbsentRoot(tree_id, logger);
                    if (not has_tree) {
                        return;
                    }
                    if (*has_tree) {
                        // set workspace root as absent
                        (*setter)(
                            std::pair(nlohmann::json::array(
                                          {FileRoot::kGitTreeMarker, tree_id}),
                                      /*is_cache_hit=*/false));
                        return;
                    }
                    // try to see if serve endpoint has the information to
                    // prepare the root itself
                    auto serve_result =
                        ServeApi::Instance().RetrieveTreeFromDistdir(
                            key.content_list,
                            /*sync_tree=*/false);
                    if (std::holds_alternative<std::string>(serve_result)) {
                        // if serve has set up the tree, it must match what we
                        // expect
                        auto const& served_tree_id =
                            std::get<std::string>(serve_result);
                        if (tree_id != served_tree_id) {
                            (*logger)(
                                fmt::format("Mismatch in served root tree "
                                            "id:\nexpected {}, but got {}",
                                            tree_id,
                                            served_tree_id),
                                /*fatal=*/true);
                            return;
                        }
                        // set workspace root as absent
                        (*setter)(
                            std::pair(nlohmann::json::array(
                                          {FileRoot::kGitTreeMarker, tree_id}),
                                      /*is_cache_hit=*/false));
                        return;
                    }
                    // check if serve failure was due to distdir content not
                    // being found or it is otherwise fatal
                    auto const& is_fatal = std::get<bool>(serve_result);
                    if (is_fatal) {
                        (*logger)(
                            fmt::format("Serve endpoint failed to set up root "
                                        "from known distdir content {}",
                                        key.content_id),
                            /*fatal=*/true);
                        return;
                    }
                    // we cannot continue without a suitable remote set up
                    if (not remote_api) {
                        (*logger)(fmt::format(
                                      "Cannot create workspace root {} as "
                                      "absent for the provided serve endpoint.",
                                      tree_id),
                                  /*fatal=*/true);
                        return;
                    }
                    // try to supply the serve endpoint with the tree via the
                    // remote CAS
                    if (remote_api.value()->IsAvailable({digest})) {
                        // tell serve to set up the root from the remote CAS
                        // tree; upload can be skipped
                        if (EnsureAbsentRootOnServe(
                                tree_id,
                                /*repo_path=*/"",
                                /*remote_api=*/std::nullopt,
                                logger,
                                /*no_sync_is_fatal=*/true)) {
                            // set workspace root as absent
                            (*setter)(std::pair(
                                nlohmann::json::array(
                                    {FileRoot::kGitTreeMarker, tree_id}),
                                /*is_cache_hit=*/false));
                            return;
                        }
                        (*logger)(fmt::format("Serve endpoint failed to create "
                                              "workspace root {} that locally "
                                              "was marked absent.",
                                              tree_id),
                                  /*fatal=*/true);
                        return;
                    }
                    // check if we have the tree in local CAS; if yes, upload it
                    // to remote for the serve endpoint to find it
                    if (local_api->IsAvailable({digest})) {
                        if (not local_api->RetrieveToCas(
                                {Artifact::ObjectInfo{
                                    .digest = digest,
                                    .type = ObjectType::Tree}},
                                *remote_api)) {
                            (*logger)(fmt::format("Failed to sync tree {} from "
                                                  "local CAS with remote CAS.",
                                                  tree_id),
                                      /*fatal=*/true);
                            return;
                        }
                        // tell serve to set up the root from the remote CAS
                        // tree; upload can be skipped
                        if (EnsureAbsentRootOnServe(
                                tree_id,
                                /*repo_path=*/"",
                                /*remote_api=*/std::nullopt,
                                logger,
                                /*no_sync_is_fatal=*/true)) {
                            // set workspace root as absent
                            (*setter)(std::pair(
                                nlohmann::json::array(
                                    {FileRoot::kGitTreeMarker, tree_id}),
                                /*is_cache_hit=*/false));
                            return;
                        }
                    }
                    // cannot create absent root with given information
                    (*logger)(
                        fmt::format("Serve endpoint failed to create workspace "
                                    "root {} that locally was marked absent.",
                                    tree_id),
                        /*fatal=*/true);
                    return;
                }
                // give warning
                (*logger)(fmt::format("Workspace root {} marked absent but no "
                                      "serve endpoint provided.",
                                      tree_id),
                          /*fatal=*/false);
                // set workspace root as absent
                (*setter)(std::pair(
                    nlohmann::json::array({FileRoot::kGitTreeMarker, tree_id}),
                    false /*no cache hit*/));
                return;
            }

            // if the root is not-absent, the order of checks is different;
            // first, look in the local CAS
            if (local_api->IsAvailable({digest})) {
                ImportFromCASAndSetRoot(key,
                                        distdir_tree_id_file,
                                        import_to_git_map,
                                        ts,
                                        setter,
                                        logger);
                // done
                return;
            }
            // now ask serve endpoint if it can set up the root; as this is for
            // a present root, a corresponding remote endpoint is needed
            if (serve_api_exists and remote_api) {
                auto serve_result =
                    ServeApi::Instance().RetrieveTreeFromDistdir(
                        key.content_list,
                        /*sync_tree=*/true);
                if (std::holds_alternative<std::string>(serve_result)) {
                    // if serve has set up the tree, it must match what we
                    // expect
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
                    // we only need the serve endpoint to try to set up the
                    // root, as we will check the remote CAS for the resulting
                    // tree anyway
                }
                else {
                    // check if serve failure was due to distdir content not
                    // being found or it is otherwise fatal
                    auto const& is_fatal = std::get<bool>(serve_result);
                    if (is_fatal) {
                        (*logger)(
                            fmt::format("Serve endpoint failed to set up root "
                                        "from known distdir content {}",
                                        key.content_id),
                            /*fatal=*/true);
                        return;
                    }
                }
            }

            // we could not set the root as present using the CAS tree
            // invariant, so now we need to ensure we have all individual blobs
            content_cas_map->ConsumeAfterKeysReady(
                ts,
                *key.repos_to_fetch,
                [distdir_tree_id_file,
                 key,
                 import_to_git_map,
                 ts,
                 setter,
                 logger]([[maybe_unused]] auto const& values) {
                    // archive blobs are in CAS
                    ImportFromCASAndSetRoot(key,
                                            distdir_tree_id_file,
                                            import_to_git_map,
                                            ts,
                                            setter,
                                            logger);
                },
                [logger, content_id = key.content_id](auto const& msg,
                                                      bool fatal) {
                    (*logger)(fmt::format("While fetching archives for distdir "
                                          "content {}:\n{}",
                                          content_id,
                                          msg),
                              fatal);
                });
        }
    };
    return AsyncMapConsumer<DistdirInfo, std::pair<nlohmann::json, bool>>(
        distdir_to_git, jobs);
}
