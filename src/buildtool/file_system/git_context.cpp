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

#include "src/buildtool/file_system/git_context.hpp"

#include "gsl/gsl"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

extern "C" {
#include <git2.h>
}

GitContext::GitContext() noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
    initialized_ = git_libgit2_init() >= 0;
    if (not initialized_) {
        Logger::Log(LogLevel::Error, "initializing libgit2 failed");
    }
#endif
}

GitContext::~GitContext() noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (initialized_) {
        git_libgit2_shutdown();
    }
#endif
}

void GitContext::Create() noexcept {
    static GitContext git_state{};
    Expects(git_state.initialized_);
}
