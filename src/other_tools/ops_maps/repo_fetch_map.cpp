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

#include "src/other_tools/ops_maps/repo_fetch_map.hpp"

#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/just_mr/utils.hpp"

auto CreateRepoFetchMap(gsl::not_null<ContentCASMap*> const& content_cas_map,
                        std::filesystem::path const& fetch_dir,
                        std::size_t jobs) -> RepoFetchMap {
    auto fetch_repo = [content_cas_map, fetch_dir](auto ts,
                                                   auto setter,
                                                   auto logger,
                                                   auto /* unused */,
                                                   auto const& key) {
        // get corresponding distfile
        auto distfile =
            (key.archive.distfile ? key.archive.distfile.value()
                                  : std::filesystem::path(key.archive.fetch_url)
                                        .filename()
                                        .string());
        // if archive available as a git tree ID stored to file,
        // that's good enough, as it means it needs no fetching
        auto tree_id_file = JustMR::Utils::GetArchiveTreeIDFile(
            key.repo_type, key.archive.content);
        if (not FileSystemManager::Exists(tree_id_file)) {
            // start work reporting
            JustMRProgress::Instance().TaskTracker().Start(key.archive.origin);
            JustMRStatistics::Instance().IncrementQueuedCounter();
            // make sure content is in CAS
            content_cas_map->ConsumeAfterKeysReady(
                ts,
                {key.archive},
                [fetch_dir,
                 content = key.archive.content,
                 distfile,
                 origin = key.archive.origin,
                 setter,
                 logger]([[maybe_unused]] auto const& values) {
                    // content is now in CAS
                    // copy content from CAS into fetch_dir
                    auto const& casf = LocalCAS<ObjectType::File>::Instance();
                    auto content_path =
                        casf.BlobPath(ArtifactDigest(content, 0, false));
                    if (content_path) {
                        auto target_name = fetch_dir / distfile;
                        if (FileSystemManager::Exists(target_name)) {
                            std::filesystem::permissions(
                                target_name,
                                std::filesystem::perms::owner_write,
                                std::filesystem::perm_options::add);
                        }
                        if (not FileSystemManager::CopyFile(*content_path,
                                                            target_name)) {
                            (*logger)(fmt::format(
                                          "Failed to copy content {} from CAS "
                                          "to {}",
                                          content,
                                          target_name.string()),
                                      /*fatal=*/true);
                            return;
                        }
                        // success
                        (*setter)(true);
                        // report work done
                        JustMRProgress::Instance().TaskTracker().Stop(origin);
                        JustMRStatistics::Instance().IncrementExecutedCounter();
                    }
                    else {
                        (*logger)(
                            fmt::format("Content {} could not be found in CAS",
                                        content),
                            /*fatal=*/true);
                        return;
                    }
                },
                [logger, content = key.archive.content](auto const& msg,
                                                        bool fatal) {
                    (*logger)(
                        fmt::format("While ensuring content {} is in CAS:\n{}",
                                    content,
                                    msg),
                        fatal);
                });
        }
        else {
            // copy content from CAS into fetch_dir
            auto const& casf = LocalCAS<ObjectType::File>::Instance();
            auto content_path =
                casf.BlobPath(ArtifactDigest(key.archive.content, 0, false));
            if (content_path) {
                auto target_name = fetch_dir / distfile;
                if (FileSystemManager::Exists(target_name)) {
                    std::filesystem::permissions(
                        target_name,
                        std::filesystem::perms::owner_write,
                        std::filesystem::perm_options::add);
                }
                if (not FileSystemManager::CopyFile(*content_path,
                                                    target_name)) {
                    (*logger)(fmt::format("Failed to copy content {} from CAS "
                                          "to {}",
                                          key.archive.content,
                                          target_name.string()),
                              /*fatal=*/true);
                    return;
                }
                // success
                (*setter)(true);
                // report cache hit
                JustMRStatistics::Instance().IncrementCacheHitsCounter();
            }
            else {
                (*logger)(fmt::format("Content {} could not be found in CAS",
                                      key.archive.content),
                          /*fatal=*/true);
                return;
            }
        }
    };
    return AsyncMapConsumer<ArchiveRepoInfo, bool>(fetch_repo, jobs);
}
