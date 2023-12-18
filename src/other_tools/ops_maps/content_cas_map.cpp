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

#include "fmt/core.h"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/utils/content.hpp"
#include "src/other_tools/utils/curl_url_handle.hpp"

auto CreateContentCASMap(LocalPathsPtr const& just_mr_paths,
                         MirrorsPtr const& additional_mirrors,
                         CAInfoPtr const& ca_info,
                         bool serve_api_exists,
                         IExecutionApi* local_api,
                         IExecutionApi* remote_api,
                         std::size_t jobs) -> ContentCASMap {
    auto ensure_in_cas = [just_mr_paths,
                          additional_mirrors,
                          ca_info,
                          serve_api_exists,
                          local_api,
                          remote_api](auto /*unused*/,
                                      auto setter,
                                      auto logger,
                                      auto /*unused*/,
                                      auto const& key) {
        auto const& cas = Storage::Instance().CAS();
        auto digest = ArtifactDigest(key.content, 0, false);
        // separate logic if we need a pure fetch
        if (key.fetch_only) {
            if (cas.BlobPath(digest, /*is_executable=*/false)) {
                (*setter)(nullptr);
                return;
            }
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
            if (serve_api_exists and
                ServeApi::ContentInRemoteCAS(key.content)) {
                // try to get content from remote CAS
                if (remote_api != nullptr and local_api != nullptr and
                    remote_api->RetrieveToCas(
                        {Artifact::ObjectInfo{.digest = digest,
                                              .type = ObjectType::File}},
                        local_api)) {
                    JustMRProgress::Instance().TaskTracker().Stop(key.origin);
                    (*setter)(nullptr);
                    return;
                }
            }
        }
        // check if content is in remote CAS, if a remote is given
        if (remote_api and local_api and remote_api->IsAvailable(digest) and
            remote_api->RetrieveToCas(
                {Artifact::ObjectInfo{.digest = digest,
                                      .type = ObjectType::File}},
                local_api)) {
            JustMRProgress::Instance().TaskTracker().Stop(key.origin);
            (*setter)(nullptr);
            return;
        }
        // archive needs network fetching;
        // first, check that mandatory fields are provided
        if (key.fetch_url.empty()) {
            (*logger)("Failed to provide archive fetch url!",
                      /*fatal=*/true);
            return;
        }
        // now do the actual fetch
        auto res = NetworkFetchWithMirrors(
            key.fetch_url, key.mirrors, ca_info, additional_mirrors);
        auto data =
            std::get_if<1>(&res);  // get pointer to fetched data, or nullptr
        if (not data) {
            (*logger)(fmt::format("Failed to fetch a file with id {} from "
                                  "provided remotes:{}",
                                  key.content,
                                  std::get<0>(res)),
                      /*fatal=*/true);
            return;
        }
        // check content wrt checksums
        if (key.sha256) {
            auto actual_sha256 =
                GetContentHash<Hasher::HashType::SHA256>(*data);
            if (actual_sha256 != key.sha256.value()) {
                (*logger)(
                    fmt::format("SHA256 mismatch for {}: expected {}, got {}",
                                key.fetch_url,
                                key.sha256.value(),
                                actual_sha256),
                    /*fatal=*/true);
                return;
            }
        }
        if (key.sha512) {
            auto actual_sha512 =
                GetContentHash<Hasher::HashType::SHA512>(*data);
            if (actual_sha512 != key.sha512.value()) {
                (*logger)(
                    fmt::format("SHA512 mismatch for {}: expected {}, got {}",
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
        if (not cas.BlobPath(digest, /*is_executable=*/false)) {
            (*logger)(fmt::format(
                          "Content {} was not found at given fetch location {}",
                          key.content,
                          key.fetch_url),
                      /*fatal=*/true);
            return;
        }
        if (key.fetch_only) {
            JustMRProgress::Instance().TaskTracker().Stop(key.origin);
        }
        // success!
        (*setter)(nullptr);
    };
    return AsyncMapConsumer<ArchiveContent, std::nullptr_t>(ensure_in_cas,
                                                            jobs);
}
