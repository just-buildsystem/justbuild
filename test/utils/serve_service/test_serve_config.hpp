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

#ifndef INCLUDED_SRC_TEST_UTILS_SERVE_SERVICE_TEST_SERVE_CONFIG_HPP
#define INCLUDED_SRC_TEST_UTILS_SERVE_SERVICE_TEST_SERVE_CONFIG_HPP

#include <optional>
#include <string>
#include <variant>

#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "test/utils/test_env.hpp"

class TestServeConfig final {
  public:
    [[nodiscard]] static auto ReadServeConfigFromEnvironment() noexcept
        -> std::optional<RemoteServeConfig> {
        RemoteServeConfig::Builder builder;
        auto result = builder.SetRemoteAddress(ReadRemoteServeAddressFromEnv())
                          .SetKnownRepositories(ReadRemoteServeReposFromEnv())
                          .Build();

        if (auto* config = std::get_if<RemoteServeConfig>(&result)) {
            return std::move(*config);
        }

        if (auto* error = std::get_if<std::string>(&result)) {
            Logger::Log(LogLevel::Error, *error);
            return std::nullopt;
        }

        Logger::Log(LogLevel::Error, "Unknown error occured.");
        return std::nullopt;
    }
};

#endif  // INCLUDED_SRC_TEST_UTILS_SERVE_SERVICE_TEST_SERVE_CONFIG_HPP
