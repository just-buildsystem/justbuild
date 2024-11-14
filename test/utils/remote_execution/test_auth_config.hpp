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

#ifndef INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_TEST_AUTH_CONFIG_HPP
#define INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_TEST_AUTH_CONFIG_HPP

#include <optional>
#include <utility>
#include <variant>

#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"
#include "test/utils/test_env.hpp"

class TestAuthConfig final {
  public:
    [[nodiscard]] static auto ReadFromEnvironment() noexcept
        -> std::optional<Auth> {
        Auth::TLS::Builder tls_builder;
        auto config = tls_builder.SetCACertificate(ReadTLSAuthCACertFromEnv())
                          .SetClientCertificate(ReadTLSAuthClientCertFromEnv())
                          .SetClientKey(ReadTLSAuthClientKeyFromEnv())
                          .Build();

        if (config) {
            if (*config) {
                // correctly configured TLS/SSL certification
                return *std::move(*config);
            }
            // given TLS certificates are invalid
            Logger::Log(LogLevel::Error, config->error());
            return std::nullopt;
        }
        return Auth{};  // no TLS certificates provided
    }
};

#endif  // INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_TEST_AUTH_CONFIG_HPP
