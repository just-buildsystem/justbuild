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

#include "src/other_tools/utils/curl_easy_handle.hpp"

#include <cstdio>
#include <fstream>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

extern "C" {
#include "curl/curl.h"
}

void curl_easy_closer(gsl::owner<CURL*> curl) {
    curl_easy_cleanup(curl);
}

namespace {

auto read_stream_data(gsl::not_null<std::FILE*> const& stream) noexcept
    -> std::string {
    // obtain stream size
    std::fseek(stream, 0, SEEK_END);
    auto size = std::ftell(stream);
    std::rewind(stream);

    // create string buffer to hold stream content
    std::string content(static_cast<size_t>(size), '\0');

    // read stream content into string buffer
    auto n = std::fread(content.data(), 1, content.size(), stream);
    if (n != static_cast<size_t>(size)) {
        Logger::Log(LogLevel::Warning,
                    "Reading curl log from temporary file failed: read only {} "
                    "bytes while {} were expected",
                    n,
                    size);
    }
    return content;
}

}  // namespace

auto CurlEasyHandle::Create() noexcept -> std::shared_ptr<CurlEasyHandle> {
    return Create(false, std::nullopt);
}

auto CurlEasyHandle::Create(
    bool no_ssl_verify,
    std::optional<std::filesystem::path> const& ca_bundle) noexcept
    -> std::shared_ptr<CurlEasyHandle> {
    try {
        auto curl = std::make_shared<CurlEasyHandle>();
        auto* handle = curl_easy_init();
        if (handle == nullptr) {
            return nullptr;
        }
        curl->handle_.reset(handle);
        // store CA info
        curl->no_ssl_verify_ = no_ssl_verify;
        curl->ca_bundle_ = ca_bundle;
        return curl;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "create curl easy handle failed with:\n{}",
                    ex.what());
        return nullptr;
    }
}

auto CurlEasyHandle::EasyWriteToFile(gsl::owner<char*> data,
                                     size_t size,
                                     size_t nmemb,
                                     gsl::owner<void*> userptr)
    -> std::streamsize {
    auto actual_size = static_cast<std::streamsize>(size * nmemb);
    auto* file = static_cast<std::ofstream*>(userptr);
    file->write(data, actual_size);  // append chunk
    return actual_size;
}

auto CurlEasyHandle::EasyWriteToString(gsl::owner<char*> data,
                                       size_t size,
                                       size_t nmemb,
                                       gsl::owner<void*> userptr)
    -> std::streamsize {
    size_t actual_size = size * nmemb;
    (static_cast<std::string*>(userptr))->append(data, actual_size);
    return static_cast<std::streamsize>(actual_size);
}

auto CurlEasyHandle::DownloadToFile(
    std::string const& url,
    std::filesystem::path const& file_path) noexcept -> int {
    // create temporary file to capture curl debug output
    gsl::owner<std::FILE*> tmp_file = std::tmpfile();
    try {
        // set URL
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());

        // ensure redirects are allowed, otherwise it might simply read empty
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 1);

        // ensure failure on error codes that otherwise might return OK
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_FAILONERROR, 1);

        // set callback for writing to file
        std::ofstream file(file_path.c_str(), std::ios::binary);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, EasyWriteToFile);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(
            handle_.get(), CURLOPT_WRITEDATA, static_cast<void*>(&file));

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_VERBOSE, 1);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_STDERR, tmp_file);

        // set SSL options
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(),
                         CURLOPT_SSL_VERIFYPEER,
                         static_cast<int>(not no_ssl_verify_));
        if (ca_bundle_) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
            curl_easy_setopt(
                handle_.get(), CURLOPT_CAINFO, ca_bundle_->c_str());
        }

        // perform download
        auto res = curl_easy_perform(handle_.get());

        // close file
        file.close();

        // check result
        if (res != CURLE_OK) {
            // cleanup failed downloaded file, if created
            [[maybe_unused]] auto tmp_res =
                FileSystemManager::RemoveFile(file_path);
            Logger::Log(LogLevel::Error, [&tmp_file]() {
                return fmt::format("curl download to file failed:\n{}",
                                   read_stream_data(tmp_file));
            });
            std::fclose(tmp_file);
            return 1;
        }

        // print curl debug output if log level is tracing
        Logger::Log(LogLevel::Trace, [&tmp_file]() {
            return fmt::format("stderr of curl downloading to file:\n{}",
                               read_stream_data(tmp_file));
        });
        std::fclose(tmp_file);
        return res;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error, [&ex, &tmp_file]() {
            return fmt::format(
                "curl download to file failed with:\n{}\n"
                "while performing:\n{}",
                ex.what(),
                read_stream_data(tmp_file));
        });
        std::fclose(tmp_file);
        return 1;
    }
}

auto CurlEasyHandle::DownloadToString(std::string const& url) noexcept
    -> std::optional<std::string> {
    // create temporary file to capture curl debug output
    gsl::owner<std::FILE*> tmp_file = std::tmpfile();
    try {
        // set URL
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_URL, url.c_str());

        // ensure redirects are allowed, otherwise it might simply read empty
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_FOLLOWLOCATION, 1);

        // ensure failure on error codes that otherwise might return OK
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_FAILONERROR, 1);

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

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(), CURLOPT_STDERR, tmp_file);

        // set SSL options
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        curl_easy_setopt(handle_.get(),
                         CURLOPT_SSL_VERIFYPEER,
                         static_cast<int>(not no_ssl_verify_));
        if (ca_bundle_) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
            curl_easy_setopt(
                handle_.get(), CURLOPT_CAINFO, ca_bundle_->c_str());
        }

        // perform download
        auto res = curl_easy_perform(handle_.get());

        // check result
        if (res != CURLE_OK) {
            Logger::Log(LogLevel::Error, [&tmp_file]() {
                return fmt::format("curl download to string failed:\n{}",
                                   read_stream_data(tmp_file));
            });
            std::fclose(tmp_file);
            return std::nullopt;
        }

        // print curl debug output if log level is tracing
        Logger::Log(LogLevel::Trace, [&tmp_file]() {
            return fmt::format("stderr of curl downloading to string:\n{}",
                               read_stream_data(tmp_file));
        });
        std::fclose(tmp_file);
        return content;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error, [&ex, &tmp_file]() {
            return fmt::format(
                "curl download to string failed with:\n{}\n"
                "while performing:\n{}",
                ex.what(),
                read_stream_data(tmp_file));
        });
        std::fclose(tmp_file);
        return std::nullopt;
    }
}
