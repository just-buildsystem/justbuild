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

#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/crypto/hasher.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/other_tools/utils/curl_easy_handle.hpp"

// Utilities related to the content of an archive

/// \brief Fetches a file from the internet and stores its content in memory.
/// Returns the content.
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

template <Hasher::HashType type>
[[nodiscard]] static auto GetContentHash(std::string const& data) noexcept
    -> std::string {
    Hasher hasher{type};
    hasher.Update(data);
    auto digest = std::move(hasher).Finalize();
    return digest.HexString();
}

#endif  // INCLUDED_SRC_OTHER_TOOLS_UTILS_CONTENT_HPP
