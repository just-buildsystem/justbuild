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

#include "src/utils/cpp/curl_easy_handle.hpp"

#include <fstream>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL
extern "C" {
#include "curl/curl.h"
}
#endif  // BOOTSTRAP_BUILD_TOOL

void curl_easy_closer(gsl::owner<CURL*> curl) {
#ifndef BOOTSTRAP_BUILD_TOOL
    curl_easy_cleanup(curl);
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto CurlEasyHandle::Create() noexcept -> std::shared_ptr<CurlEasyHandle> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return nullptr;
#else
    try {
        auto curl = std::make_shared<CurlEasyHandle>();
        auto* handle = curl_easy_init();
        if (handle == nullptr) {
            return nullptr;
        }
        curl->handle_.reset(handle);
        return curl;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "create curl easy handle failed with:\n{}",
                    ex.what());
        return nullptr;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto CurlEasyHandle::EasyWriteToFile(gsl::owner<char*> data,
                                     size_t size,
                                     size_t nmemb,
                                     gsl::owner<void*> userptr)
    -> std::streamsize {
#ifdef BOOTSTRAP_BUILD_TOOL
    return 0;
#else
    auto actual_size = static_cast<std::streamsize>(size * nmemb);
    auto* file = static_cast<std::ofstream*>(userptr);
    file->write(data, actual_size);  // append chunk
    return actual_size;
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto CurlEasyHandle::EasyWriteToString(gsl::owner<char*> data,
                                       size_t size,
                                       size_t nmemb,
                                       gsl::owner<void*> userptr)
    -> std::streamsize {
#ifdef BOOTSTRAP_BUILD_TOOL
    return 0;
#else
    size_t actual_size = size * nmemb;
    (static_cast<std::string*>(userptr))->append(data, actual_size);
    return static_cast<std::streamsize>(actual_size);
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto CurlEasyHandle::DownloadToFile(
    std::string const& url,
    std::filesystem::path const& file_path) noexcept -> int {
#ifdef BOOTSTRAP_BUILD_TOOL
    return 1;
#else
    try {
        // set URL
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());

        // ensure redirects are allowed, otherwise it might simply read empty
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 1);

        // set callback for writing to file
        std::ofstream file(file_path.c_str(), std::ios::binary);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, EasyWriteToFile);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(
            handle_.get(), CURLOPT_WRITEDATA, static_cast<void*>(&file));

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_VERBOSE, 1);

        // perform download
        auto res = curl_easy_perform(handle_.get());

        // close file
        file.close();

        // check result
        if (res != CURLE_OK) {
            // cleanup failed downloaded file, if created
            [[maybe_unused]] auto tmp_res =
                FileSystemManager::RemoveFile(file_path);
        }
        return res;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "curl download to file failed with:\n{}",
                    ex.what());
        return 1;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto CurlEasyHandle::DownloadToString(std::string const& url) noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // set URL
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());

        // ensure redirects are allowed, otherwise it might simply read empty
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 1);

        // set callback for writing to string
        std::string content{};

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(
            handle_.get(), CURLOPT_WRITEFUNCTION, EasyWriteToString);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(
            handle_.get(), CURLOPT_WRITEDATA, static_cast<void*>(&content));

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_VERBOSE, 1);

        // perform download
        auto res = curl_easy_perform(handle_.get());

        // check result
        if (res != CURLE_OK) {
            return std::nullopt;
        }
        return content;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "curl download to string failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}
