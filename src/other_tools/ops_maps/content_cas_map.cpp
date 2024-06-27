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

#include "src/other_tools/ops_maps/content_cas_map.hpp"

#include <utility>  // std::move

#include "fmt/core.h"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/utils/content.hpp"
#include "src/other_tools/utils/curl_url_handle.hpp"

namespace {

void FetchFromNetwork(ArchiveContent const& key,
                      MirrorsPtr const& additional_mirrors,
                      CAInfoPtr const& ca_info,
                      ContentCASMap::SetterPtr const& setter,
                      ContentCASMap::LoggerPtr const& logger) {
    // first, check that mandatory fields are provided
    if (key.fetch_url.empty()) {
        (*logger)("Failed to provide archive fetch url!",
                  /*fatal=*/true);
        return;
    }
    // now do the actual fetch
    auto data = NetworkFetchWithMirrors(
        key.fetch_url, key.mirrors, ca_info, additional_mirrors);
    if (not data) {
        (*logger)(fmt::format("Failed to fetch a file with id {} from provided "
                              "remotes:{}",
                              key.content,
                              data.error()),
                  /*fatal=*/true);
        return;
    }
    // check content wrt checksums
    if (key.sha256) {
        auto actual_sha256 = GetContentHash<Hasher::HashType::SHA256>(*data);
        if (actual_sha256 != key.sha256.value()) {
            (*logger)(fmt::format("SHA256 mismatch for {}: expected {}, got {}",
                                  key.fetch_url,
                                  key.sha256.value(),
                                  actual_sha256),
                      /*fatal=*/true);
            return;
        }
    }
    if (key.sha512) {
        auto actual_sha512 = GetContentHash<Hasher::HashType::SHA512>(*data);
        if (actual_sha512 != key.sha512.value()) {
            (*logger)(fmt::format("SHA512 mismatch for {}: expected {}, got {}",
                                  key.fetch_url,
                                  key.sha512.value(),
                                  actual_sha512),
                      /*fatal=*/true);
            return;
        }
    }
    // add the fetched data to CAS
    auto path = StorageUtils::AddToCAS(*data);
    // check one last time if content is in CAS now
    if (not path) {
        (*logger)(fmt::format("Failed to store fetched content from {}",
                              key.fetch_url),
                  /*fatal=*/true);
        return;
    }
    // check that the data we stored actually produces the requested digest
    auto const& cas = Storage::Instance().CAS();
    if (not cas.BlobPath(ArtifactDigest{key.content, 0, /*is_tree=*/false},
                         /*is_executable=*/false)) {
        (*logger)(
            fmt::format("Content {} was not found at given fetch location {}",
                        key.content,
                        key.fetch_url),
            /*fatal=*/true);
        return;
    }
    JustMRProgress::Instance().TaskTracker().Stop(key.origin);
    // success!
    (*setter)(nullptr);
}

}  // namespace

auto CreateContentCASMap(
    LocalPathsPtr const& just_mr_paths,
    MirrorsPtr const& additional_mirrors,
    CAInfoPtr const& ca_info,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    ServeApi const* serve,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    std::size_t jobs) -> ContentCASMap {
    auto ensure_in_cas = [just_mr_paths,
                          additional_mirrors,
                          ca_info,
                          critical_git_op_map,
                          serve,
                          local_api,
                          remote_api](auto ts,
                                      auto setter,
                                      auto logger,
                                      auto /*unused*/,
                                      auto const& key) {
        auto digest = ArtifactDigest(key.content, 0, false);
        // check local CAS
        auto const& cas = Storage::Instance().CAS();
        if (cas.BlobPath(digest, /*is_executable=*/false)) {
            (*setter)(nullptr);
            return;
        }
        // check if content is in Git cache;
        // ensure Git cache
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
            [key,
             digest,
             just_mr_paths,
             additional_mirrors,
             ca_info,
             serve,
             local_api,
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
                auto const just_git_cas = op_result.git_cas;
                // open fake repo wrap for GitCAS
                auto just_git_repo = GitRepoRemote::Open(just_git_cas);
                if (not just_git_repo) {
                    (*logger)("Could not open Git cache repository!",
                              /*fatal=*/true);
                    return;
                }
                // verify if local Git knows content blob
                auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                    [&logger, blob = key.content](auto const& msg, bool fatal) {
                        (*logger)(fmt::format("While verifying presence of "
                                              "blob {}:\n{}",
                                              blob,
                                              msg),
                                  fatal);
                    });
                auto res =
                    just_git_repo->TryReadBlob(key.content, wrapped_logger);
                if (not res.first) {
                    // blob check failed
                    return;
                }
                auto const& cas = Storage::Instance().CAS();
                if (res.second) {
                    // blob found; add it to CAS
                    if (not cas.StoreBlob(*res.second,
                                          /*is_executable=*/false)) {
                        (*logger)(fmt::format("Failed to store content {} "
                                              "to local CAS",
                                              key.content),
                                  /*fatal=*/true);
                        return;
                    }
                    // content stored to CAS
                    (*setter)(nullptr);
                    return;
                }
                // blob not found in Git cache
                JustMRProgress::Instance().TaskTracker().Start(key.origin);
                // add distfile to CAS
                auto repo_distfile =
                    (key.distfile ? key.distfile.value()
                                  : std::filesystem::path(key.fetch_url)
                                        .filename()
                                        .string());
                StorageUtils::AddDistfileToCAS(repo_distfile, just_mr_paths);
                // check if content is in CAS now
                if (cas.BlobPath(digest, /*is_executable=*/false)) {
                    JustMRProgress::Instance().TaskTracker().Stop(key.origin);
                    (*setter)(nullptr);
                    return;
                }
                // check if content is known to remote serve service
                if (serve != nullptr and remote_api != nullptr and
                    serve->ContentInRemoteCAS(key.content)) {
                    // try to get content from remote CAS
                    if (remote_api->RetrieveToCas(
                            {Artifact::ObjectInfo{.digest = digest,
                                                  .type = ObjectType::File}},
                            *local_api)) {
                        JustMRProgress::Instance().TaskTracker().Stop(
                            key.origin);
                        (*setter)(nullptr);
                        return;
                    }
                }
                // check remote execution endpoint, if given
                if (remote_api != nullptr and
                    remote_api->RetrieveToCas(
                        {Artifact::ObjectInfo{.digest = digest,
                                              .type = ObjectType::File}},
                        *local_api)) {
                    JustMRProgress::Instance().TaskTracker().Stop(key.origin);
                    (*setter)(nullptr);
                    return;
                }
                // revert to network fetch
                FetchFromNetwork(
                    key, additional_mirrors, ca_info, setter, logger);
            },
            [logger, target_path = StorageConfig::GitRoot()](auto const& msg,
                                                             bool fatal) {
                (*logger)(fmt::format("While running critical Git op "
                                      "ENSURE_INIT for target {}:\n{}",
                                      target_path.string(),
                                      msg),
                          fatal);
            });
    };
    return AsyncMapConsumer<ArchiveContent, std::nullptr_t>(ensure_in_cas,
                                                            jobs);
}
