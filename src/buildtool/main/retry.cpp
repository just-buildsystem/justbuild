// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/main/retry.hpp"

#ifdef BOOTSTRAP_BUILD_TOOL

[[nodiscard]] auto SetupRetryConfig(RetryArguments const& args) -> bool {
    return true;
}

#else

#include "src/buildtool/common/remote/retry_parameters.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

[[nodiscard]] auto SetupRetryConfig(RetryArguments const& args) -> bool {
    if (args.max_attempts) {
        if (!Retry::SetMaxAttempts(*args.max_attempts)) {
            Logger::Log(LogLevel::Error, "Invalid value for max-attempts.");
            return false;
        }
    }
    if (args.initial_backoff_seconds) {
        if (!Retry::SetInitialBackoffSeconds(*args.initial_backoff_seconds)) {
            Logger::Log(LogLevel::Error,
                        "Invalid value for initial-backoff-seconds.");
            return false;
        }
    }
    if (args.max_backoff_seconds) {
        if (!Retry::SetMaxBackoffSeconds(*args.max_backoff_seconds)) {
            Logger::Log(LogLevel::Error,
                        "Invalid value for max-backoff-seconds.");
            return false;
        }
    }
    return true;
}

#endif  // BOOTSTRAP_BUILD_TOOL
