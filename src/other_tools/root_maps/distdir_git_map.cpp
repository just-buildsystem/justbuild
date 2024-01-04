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

void ImportFromCASAndSetRoot(
    std::shared_ptr<std::unordered_map<std::string, std::string>> const&
        content_list,
    std::string const& content_id,
    std::filesystem::path const& distdir_tree_id_file,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<TaskSystem*> const& ts,
    DistdirGitMap::SetterPtr const& setter,
    DistdirGitMap::LoggerPtr const& logger) {
    // create the links to CAS
    auto tmp_dir = StorageUtils::CreateTypedTmpDir("distdir");
    if (not tmp_dir) {
        (*logger)(fmt::format("Failed to create tmp path for "
                              "distdir target {}",
                              content_id),
                  /*fatal=*/true);
        return;
    }
    // link content from CAS into tmp dir
    if (not LinkToCAS(content_list, tmp_dir->GetPath())) {
        (*logger)(
            fmt::format("Failed to create links to CAS content!", content_id),
            /*fatal=*/true);
        return;
    }
    // do import to git
    CommitInfo c_info{tmp_dir->GetPath(), "distdir", content_id};
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
                false /*no cache hit*/));
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
    IExecutionApi* local_api,
    IExecutionApi* remote_api,
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
                 absent = key.absent,
                 setter,
                 logger](auto const& values) {
                    GitOpValue op_result = *values[0];
                    // check flag
                    if (not op_result.result) {
                        (*logger)("Git init failed",
                                  /*fatal=*/true);
                        return;
                    }
                    // subdir is ".", so no need to deal with the Git cache
                    // set the workspace root
                    auto root = nlohmann::json::array(
                        {FileRoot::kGitTreeMarker, distdir_tree_id});
                    if (not absent) {
                        root.emplace_back(StorageConfig::GitRoot().string());
                    }
                    (*setter)(std::pair(std::move(root), true /*cache hit*/));
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
            // if pure absent, we simply set the root tree
            if (key.absent) {
                (*setter)(std::pair(
                    nlohmann::json::array({FileRoot::kGitTreeMarker, tree_id}),
                    false /*no cache hit*/));
                return;
            }
            // otherwise, we check first the serve endpoint
            if (serve_api_exists) {
                if (auto served_tree_id = ServeApi::RetrieveTreeFromDistdir(
                        key.content_list, /*sync_tree=*/true)) {
                    // sanity check
                    if (*served_tree_id != tree_id) {
                        (*logger)(
                            fmt::format("Unexpected served tree id for distdir "
                                        "content {}:\nExpected {}, but got {}",
                                        key.content_id,
                                        tree_id,
                                        *served_tree_id),
                            /*is_fatal=*/true);
                        return;
                    }
                    // get the ditfiles from remote CAS in bulk
                    std::vector<Artifact::ObjectInfo> objects{};
                    objects.reserve(key.content_list->size());
                    for (auto const& kv : *key.content_list) {
                        objects.emplace_back(Artifact::ObjectInfo{
                            .digest =
                                ArtifactDigest{kv.second, 0, /*is_tree=*/false},
                            .type = ObjectType::File});
                    }
                    if (remote_api != nullptr and local_api != nullptr and
                        remote_api->RetrieveToCas(objects, local_api)) {
                        ImportFromCASAndSetRoot(key.content_list,
                                                key.content_id,
                                                distdir_tree_id_file,
                                                import_to_git_map,
                                                ts,
                                                setter,
                                                logger);
                        // done!
                        return;
                    }
                    // just serve should have made the blobs available in the
                    // remote CAS, so log this attempt and revert to default
                    // fetch each blob individually
                    (*logger)(fmt::format("Tree {} marked as served, but not "
                                          "all distfile blobs found on remote",
                                          *served_tree_id),
                              /*fatal=*/false);
                }
                else {
                    // give warning
                    (*logger)(
                        fmt::format("Distdir content {} could not be served",
                                    key.content_id),
                        /*fatal=*/false);
                }
            }
            else {
                // give warning
                (*logger)(
                    fmt::format("Missing serve endpoint for distdir {} marked "
                                "absent requires slower network fetch.",
                                key.content_id),
                    /*fatal=*/false);
            }
            // revert to fetching the gathered distdir repos into CAS
            content_cas_map->ConsumeAfterKeysReady(
                ts,
                *key.repos_to_fetch,
                [distdir_tree_id_file,
                 content_id = key.content_id,
                 content_list = key.content_list,
                 import_to_git_map,
                 ts,
                 setter,
                 logger]([[maybe_unused]] auto const& values) mutable {
                    // repos are in CAS
                    ImportFromCASAndSetRoot(content_list,
                                            content_id,
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
