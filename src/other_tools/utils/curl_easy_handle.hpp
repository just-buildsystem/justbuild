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

#ifndef INCLUDED_SRC_OTHER_TOOLS_UTILS_CURL_EASY_HANDLE_HPP
#define INCLUDED_SRC_OTHER_TOOLS_UTILS_CURL_EASY_HANDLE_HPP

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/logging/log_level.hpp"
#include "src/other_tools/utils/curl_context.hpp"

extern "C" {
#if defined(BUILDING_LIBCURL) || defined(CURL_STRICTER)
using CURL = struct Curl_easy;
#else
using CURL = void;
#endif
}

void curl_easy_closer(gsl::owner<CURL*> curl);

class CurlEasyHandle {
  public:
    CurlEasyHandle() noexcept = default;
    ~CurlEasyHandle() noexcept = default;

    // prohibit moves and copies
    CurlEasyHandle(CurlEasyHandle const&) = delete;
    CurlEasyHandle(CurlEasyHandle&& other) = delete;
    auto operator=(CurlEasyHandle const&) = delete;
    auto operator=(CurlEasyHandle&& other) = delete;

    /// \brief Create a CurlEasyHandle object
    [[nodiscard]] auto static Create(
        LogLevel log_level = LogLevel::Error) noexcept
        -> std::shared_ptr<CurlEasyHandle>;

    /// \brief Create a CurlEasyHandle object with non-default CA info
    [[nodiscard]] auto static Create(
        bool no_ssl_verify,
        std::optional<std::filesystem::path> const& ca_bundle,
        LogLevel log_level = LogLevel::Error) noexcept
        -> std::shared_ptr<CurlEasyHandle>;

    /// \brief Download file from URL into given file_path.
    /// Will perform cleanup (i.e., remove empty file) in case download fails.
    /// Returns 0 if successful.
    [[nodiscard]] auto DownloadToFile(
        std::string const& url,
        std::filesystem::path const& file_path) noexcept -> int;

    /// \brief Download file from URL into string as binary.
    /// Returns the content or nullopt if download failure.
    [[nodiscard]] auto DownloadToString(std::string const& url) noexcept
        -> std::optional<std::string>;

  private:
    // IMPORTANT: the CurlContext must to be initialized before any curl object!
    CurlContext curl_context_;
    std::unique_ptr<CURL, decltype(&curl_easy_closer)> handle_{
        nullptr,
        curl_easy_closer};
    // allow also non-fatal logging of curl operations
    LogLevel log_level_{};

    bool no_ssl_verify_{false};
    std::optional<std::filesystem::path> ca_bundle_{std::nullopt};

    /// \brief Overwrites write_callback to redirect to file instead of stdout.
    [[nodiscard]] auto static EasyWriteToFile(gsl::owner<char*> data,
                                              std::size_t size,
                                              std::size_t nmemb,
                                              gsl::owner<void*> userptr)
        -> std::streamsize;

    /// \brief Overwrites write_callback to redirect to string instead of
    /// stdout.
    [[nodiscard]] auto static EasyWriteToString(gsl::owner<char*> data,
                                                std::size_t size,
                                                std::size_t nmemb,
                                                gsl::owner<void*> userptr)
        -> std::streamsize;
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_UTILS_CURL_EASY_HANDLE_HPP
