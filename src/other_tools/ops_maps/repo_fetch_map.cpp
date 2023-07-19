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

#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/storage/storage.hpp"
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
        // check if content not already in CAS
        auto const& cas = Storage::Instance().CAS();
        auto content_path =
            cas.BlobPath(ArtifactDigest(key.archive.content, 0, false),
                         /*is_executable=*/false);
        if (not content_path) {
            // make sure content is in CAS
            content_cas_map->ConsumeAfterKeysReady(
                ts,
                {key.archive},
                [fetch_dir,
                 content = key.archive.content,
                 distfile,
                 setter,
                 logger]([[maybe_unused]] auto const& values) {
                    // content is now in CAS, so copy content into fetch_dir
                    auto const& cas = Storage::Instance().CAS();
                    auto content_path =
                        cas.BlobPath(ArtifactDigest(content, 0, false),
                                     /*is_executable=*/false)
                            .value();
                    auto target_name = fetch_dir / distfile;
                    if (FileSystemManager::Exists(target_name)) {
                        std::filesystem::permissions(
                            target_name,
                            std::filesystem::perms::owner_write,
                            std::filesystem::perm_options::add);
                    }
                    if (not FileSystemManager::CopyFile(content_path,
                                                        target_name)) {
                        (*logger)(
                            fmt::format("Failed to copy content {} from CAS "
                                        "to {}",
                                        content,
                                        target_name.string()),
                            /*fatal=*/true);
                        return;
                    }
                    // success
                    JustMRStatistics::Instance().IncrementExecutedCounter();
                    (*setter)(true);
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
            auto target_name = fetch_dir / distfile;
            if (FileSystemManager::Exists(target_name)) {
                std::filesystem::permissions(
                    target_name,
                    std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::add);
            }
            if (not FileSystemManager::CopyFile(*content_path, target_name)) {
                (*logger)(fmt::format("Failed to copy content {} from CAS "
                                      "to {}",
                                      key.archive.content,
                                      target_name.string()),
                          /*fatal=*/true);
                return;
            }
            // success
            JustMRStatistics::Instance().IncrementCacheHitsCounter();
            (*setter)(true);
        }
    };
    return AsyncMapConsumer<ArchiveRepoInfo, bool>(fetch_repo, jobs);
}
