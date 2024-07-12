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

#ifndef INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_TEST_REMOTE_CONFIG_HPP
#define INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_TEST_REMOTE_CONFIG_HPP

#include <optional>

#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "test/utils/test_env.hpp"

class TestRemoteConfig final {
  public:
    [[nodiscard]] static auto ReadFromEnvironment() noexcept
        -> std::optional<RemoteExecutionConfig> {
        RemoteExecutionConfig::Builder builder;
        auto config =
            builder.SetRemoteAddress(ReadRemoteAddressFromEnv())
                .SetPlatformProperties(ReadPlatformPropertiesFromEnv())
                .Build();

        if (config) {
            return *std::move(config);
        }

        Logger::Log(LogLevel::Error, config.error());
        return std::nullopt;
    }
};

#endif  // INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_TEST_REMOTE_CONFIG_HPP
