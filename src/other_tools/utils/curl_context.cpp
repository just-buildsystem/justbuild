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

#include "src/other_tools/utils/curl_context.hpp"

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

extern "C" {
#include <curl/curl.h>
}

CurlContext::CurlContext() noexcept {
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    initialized_ = curl_global_init(CURL_GLOBAL_DEFAULT) >= 0;
    if (not initialized_) {
        Logger::Log(LogLevel::Error, "initializing libcurl failed");
    }
}

CurlContext::~CurlContext() noexcept {
    if (initialized_) {
        curl_global_cleanup();
    }
}
