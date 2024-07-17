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

#include <utility>  // std::move

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

[[nodiscard]] auto CreateRetryConfig(RetryArguments const& args)
    -> std::optional<RetryConfig> {
    RetryConfig::Builder builder;
    auto config = builder.SetInitialBackoffSeconds(args.initial_backoff_seconds)
                      .SetMaxBackoffSeconds(args.max_backoff_seconds)
                      .SetMaxAttempts(args.max_attempts)
                      .Build();

    if (config) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}
