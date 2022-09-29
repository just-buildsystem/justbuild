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

#include "src/utils/cpp/curl_context.hpp"

#include "src/buildtool/logging/logger.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL
extern "C" {
#include <curl/curl.h>
}
#endif  // BOOTSTRAP_BUILD_TOOL

CurlContext::CurlContext() noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    if (not(initialized_ = (curl_global_init(CURL_GLOBAL_DEFAULT) >= 0))) {
        Logger::Log(LogLevel::Error, "initializing libcurl failed");
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

CurlContext::~CurlContext() noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (initialized_) {
        curl_global_cleanup();
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}