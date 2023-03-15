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

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/other_tools/utils/curl_context.hpp"
#include "src/other_tools/utils/curl_easy_handle.hpp"

// The caller of this test needs to make sure the port is given as content of
// the file "port.txt" in the directory where this test is run
[[nodiscard]] auto getPort() noexcept -> std::string {
    // read file where port has to be given
    auto port = FileSystemManager::ReadFile(std::filesystem::path("port.txt"));
    REQUIRE(port);
    // strip any end terminator
    std::erase_if(*port, [](auto ch) { return (ch == '\n' or ch == '\r'); });
    return *port;
}

TEST_CASE("Curl context", "[curl_context]") {
    CurlContext curl_context{};
}

TEST_CASE("Curl easy handle", "[curl_easy_handle]") {
    auto kServerUrl = std::string("http://127.0.0.1:") + getPort() +
                      std::string("/test_file.txt");
    auto kTargetDir =
        std::filesystem::path(std::getenv("TEST_TMPDIR")) / "target_dir";

    // make target dir
    CHECK(FileSystemManager::CreateDirectory(kTargetDir));
    // create handle
    auto curl_handle = CurlEasyHandle::Create();
    REQUIRE(curl_handle);

    SECTION("Curl download to file") {
        // download test file from local HTTP server into new location
        auto file_path = kTargetDir / "test_file.txt";
        REQUIRE(curl_handle->DownloadToFile(kServerUrl, file_path) == 0);
        REQUIRE(FileSystemManager::IsFile(file_path));
    }

    SECTION("Curl download to string") {
        // download test file from local HTTP server into string
        auto content = curl_handle->DownloadToString(kServerUrl);
        REQUIRE(content);
        REQUIRE(*content == "test\n");
    }
}
