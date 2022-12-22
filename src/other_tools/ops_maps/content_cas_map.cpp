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

#include "src/buildtool/crypto/hasher.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/utils/cpp/curl_easy_handle.hpp"

namespace {

/// \brief Fetches a file from the internet and stores its content in memory.
/// Returns the content.
[[nodiscard]] auto NetworkFetch(std::string const& fetch_url) noexcept
    -> std::optional<std::string> {
    auto curl_handle = CurlEasyHandle::Create();
    if (not curl_handle) {
        return std::nullopt;
    }
    return curl_handle->DownloadToString(fetch_url);
}

template <Hasher::HashType type>
[[nodiscard]] auto GetContentHash(std::string const& data) noexcept
    -> std::string {
    Hasher hasher{type};
    hasher.Update(data);
    auto digest = std::move(hasher).Finalize();
    return digest.HexString();
}

}  // namespace

auto CreateContentCASMap(JustMR::PathsPtr const& just_mr_paths,
                         std::size_t jobs) -> ContentCASMap {
    auto ensure_in_cas = [just_mr_paths](auto /*unused*/,
                                         auto setter,
                                         auto logger,
                                         auto /*unused*/,
                                         auto const& key) {
        // check if content already in CAS
        auto const& casf = LocalCAS<ObjectType::File>::Instance();
        auto digest = ArtifactDigest(key.content, 0, false);
        if (casf.BlobPath(digest)) {
            (*setter)(true);
            return;
        }
        // add distfile to CAS
        auto repo_distfile =
            (key.distfile
                 ? key.distfile.value()
                 : std::filesystem::path(key.fetch_url).filename().string());
        JustMR::Utils::AddDistfileToCAS(repo_distfile, just_mr_paths);
        // check if content is in CAS now
        if (casf.BlobPath(digest)) {
            (*setter)(true);
            return;
        }
        // archive needs fetching
        // before any network fetching, check that mandatory fields are provided
        if (key.fetch_url.empty()) {
            (*logger)("Failed to provide archive fetch url!",
                      /*fatal=*/true);
            return;
        }
        // now do the actual fetch
        auto data = NetworkFetch(key.fetch_url);
        if (data == std::nullopt) {
            (*logger)(fmt::format("Failed to fetch a file with id {} from {}",
                                  key.content,
                                  key.fetch_url),
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
        auto path = JustMR::Utils::AddToCAS(*data);
        // check one last time if content is in CAS now
        if (not path) {
            (*logger)(fmt::format("Failed to fetch a file with id {} from {}",
                                  key.content,
                                  key.fetch_url),
                      /*fatal=*/true);
            return;
        }
        (*setter)(true);
    };
    return AsyncMapConsumer<ArchiveContent, bool>(ensure_in_cas, jobs);
}
