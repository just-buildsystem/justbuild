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

#ifndef INCLUDED_SRC_OTHER_TOOLS_UTILS_CONTENT_HPP
#define INCLUDED_SRC_OTHER_TOOLS_UTILS_CONTENT_HPP

#include <optional>
#include <string>
#include <utility>  // std::move

#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/crypto/hasher.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/other_tools/just_mr/mirrors.hpp"
#include "src/other_tools/utils/curl_easy_handle.hpp"
#include "src/other_tools/utils/curl_url_handle.hpp"
#include "src/utils/cpp/expected.hpp"

// Utilities related to the content of an archive

/// \brief Fetches a file from the internet and stores its content in memory.
/// \returns the content.
[[nodiscard]] static auto NetworkFetch(std::string const& fetch_url,
                                       CAInfoPtr const& ca_info) noexcept
    -> std::optional<std::string> {
    auto curl_handle = CurlEasyHandle::Create(
        ca_info->no_ssl_verify, ca_info->ca_bundle, LogLevel::Debug);
    if (not curl_handle) {
        return std::nullopt;
    }
    return curl_handle->DownloadToString(fetch_url);
}

/// \brief Fetches a file from the internet and stores its content in memory.
/// Tries not only a given remote, but also all associated remote locations.
/// \returns The fetched data on success or an unexpected error as string.
[[nodiscard]] static auto NetworkFetchWithMirrors(
    std::string const& fetch_url,
    std::vector<std::string> const& mirrors,
    CAInfoPtr const& ca_info,
    MirrorsPtr const& additional_mirrors) noexcept
    -> expected<std::string, std::string> {
    // keep all remotes tried, to report in case fetch fails
    std::string remotes_buffer{};
    std::optional<std::string> data{std::nullopt};

    // try repo url
    auto all_mirrors = std::vector<std::string>({fetch_url});
    // try repo mirrors afterwards
    all_mirrors.insert(all_mirrors.end(), mirrors.begin(), mirrors.end());

    if (auto preferred_hostnames =
            MirrorsUtils::GetPreferredHostnames(additional_mirrors);
        not preferred_hostnames.empty()) {
        all_mirrors =
            MirrorsUtils::SortByHostname(all_mirrors, preferred_hostnames);
    }

    // always try local mirrors first
    auto local_mirrors =
        MirrorsUtils::GetLocalMirrors(additional_mirrors, fetch_url);
    all_mirrors.insert(
        all_mirrors.begin(), local_mirrors.begin(), local_mirrors.end());

    for (auto const& mirror : all_mirrors) {
        if (data = NetworkFetch(mirror, ca_info); data) {
            break;
        }
        // add local mirror to buffer
        remotes_buffer.append(fmt::format("\n> {}", mirror));
    }
    if (not data) {
        return unexpected{remotes_buffer};
    }
    return *data;
}

template <Hasher::HashType kType>
[[nodiscard]] static auto GetContentHash(std::string const& data) noexcept
    -> std::string {
    auto hasher = Hasher::Create(kType);
    hasher->Update(data);
    auto digest = std::move(*hasher).Finalize();
    return digest.HexString();
}

#endif  // INCLUDED_SRC_OTHER_TOOLS_UTILS_CONTENT_HPP
