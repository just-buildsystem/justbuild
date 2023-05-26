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

#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/storage/config.hpp"
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

}  // namespace

auto CreateDistdirGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::size_t jobs) -> DistdirGitMap {
    auto distdir_to_git = [content_cas_map,
                           import_to_git_map,
                           critical_git_op_map](auto ts,
                                                auto setter,
                                                auto logger,
                                                auto /* unused */,
                                                auto const& key) {
        auto distdir_tree_id_file =
            JustMR::Utils::GetDistdirTreeIDFile(key.content_id);
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
                 ignore_special = key.ignore_special,
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
                    (*setter)(std::pair(
                        nlohmann::json::array(
                            {ignore_special
                                 ? FileRoot::kGitTreeIgnoreSpecialMarker
                                 : FileRoot::kGitTreeMarker,
                             distdir_tree_id,
                             StorageConfig::GitRoot().string()}),
                        true));
                },
                [logger, target_path = StorageConfig::GitRoot()](
                    auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While running critical Git "
                                          "op ENSURE_INIT for "
                                          "target {}:\n{}",
                                          target_path.string(),
                                          msg),
                              fatal);
                });
        }
        else {
            // fetch the gathered distdir repos into CAS
            content_cas_map->ConsumeAfterKeysReady(
                ts,
                *key.repos_to_fetch,
                [distdir_tree_id_file,
                 content_id = key.content_id,
                 content_list = key.content_list,
                 origin = key.origin,
                 ignore_special = key.ignore_special,
                 import_to_git_map,
                 ts,
                 setter,
                 logger]([[maybe_unused]] auto const& values) mutable {
                    // repos are in CAS
                    // create the links to CAS
                    auto tmp_dir = JustMR::Utils::CreateTypedTmpDir("distdir");
                    if (not tmp_dir) {
                        (*logger)(fmt::format("Failed to create tmp path for "
                                              "distdir target {}",
                                              content_id),
                                  /*fatal=*/true);
                        return;
                    }
                    // link content from CAS into tmp dir
                    if (not LinkToCAS(content_list, tmp_dir->GetPath())) {
                        (*logger)(fmt::format(
                                      "Failed to create links to CAS content!",
                                      content_id),
                                  /*fatal=*/true);
                        return;
                    }
                    // do import to git
                    CommitInfo c_info{
                        tmp_dir->GetPath(), "distdir", content_id};
                    import_to_git_map->ConsumeAfterKeysReady(
                        ts,
                        {std::move(c_info)},
                        [tmp_dir,  // keep tmp_dir alive
                         distdir_tree_id_file,
                         origin,
                         ignore_special,
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
                            if (not JustMR::Utils::WriteTreeIDFile(
                                    distdir_tree_id_file, distdir_tree_id)) {
                                (*logger)(
                                    fmt::format(
                                        "Failed to write tree id to file {}",
                                        distdir_tree_id_file.string()),
                                    /*fatal=*/true);
                                return;
                            }
                            // set the workspace root
                            (*setter)(std::pair(
                                nlohmann::json::array(
                                    {ignore_special
                                         ? FileRoot::kGitTreeIgnoreSpecialMarker
                                         : FileRoot::kGitTreeMarker,
                                     distdir_tree_id,
                                     StorageConfig::GitRoot().string()}),
                                false));
                        },
                        [logger, target_path = tmp_dir->GetPath()](
                            auto const& msg, bool fatal) {
                            (*logger)(fmt::format("While importing target {} "
                                                  "to git:\n{}",
                                                  target_path.string(),
                                                  msg),
                                      fatal);
                        });
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
