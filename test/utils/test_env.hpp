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

#ifndef INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP
#define INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>

#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "test/utils/logging/log_config.hpp"

[[nodiscard]] static inline auto ReadPlatformPropertiesFromEnv()
    -> std::vector<std::string> {
    std::vector<std::string> properties{};
    auto* execution_props = std::getenv("REMOTE_EXECUTION_PROPERTIES");
    if (execution_props not_eq nullptr) {
        std::istringstream pss(std::string{execution_props});
        std::string keyval_pair;
        while (std::getline(pss, keyval_pair, ' ')) {
            properties.emplace_back(keyval_pair);
        }
    }
    return properties;
}

[[nodiscard]] static inline auto ReadCompatibilityFromEnv() noexcept
    -> std::optional<bool> {
    try {
        return std::getenv("COMPATIBLE") != nullptr;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] static inline auto ReadRemoteAddressFromEnv()
    -> std::optional<std::string> {
    auto* execution_address = std::getenv("REMOTE_EXECUTION_ADDRESS");
    return execution_address == nullptr
               ? std::nullopt
               : std::make_optional(std::string{execution_address});
}

[[nodiscard]] static inline auto ReadRemoteServeAddressFromEnv()
    -> std::optional<std::string> {
    auto* serve_address = std::getenv("REMOTE_SERVE_ADDRESS");
    return serve_address == nullptr
               ? std::nullopt
               : std::make_optional(std::string{serve_address});
}

[[nodiscard]] static inline auto ReadTLSAuthCACertFromEnv()
    -> std::optional<std::filesystem::path> {
    auto* ca_cert = std::getenv("TLS_CA_CERT");
    return ca_cert == nullptr
               ? std::nullopt
               : std::make_optional(std::filesystem::path(ca_cert));
}

[[nodiscard]] static inline auto ReadTLSAuthClientCertFromEnv()
    -> std::optional<std::filesystem::path> {
    auto* client_cert = std::getenv("TLS_CLIENT_CERT");
    return client_cert == nullptr
               ? std::nullopt
               : std::make_optional(std::filesystem::path(client_cert));
}

[[nodiscard]] static inline auto ReadTLSAuthClientKeyFromEnv()
    -> std::optional<std::filesystem::path> {
    auto* client_key = std::getenv("TLS_CLIENT_KEY");
    return client_key == nullptr
               ? std::nullopt
               : std::make_optional(std::filesystem::path(client_key));
}

[[nodiscard]] static inline auto ReadRemoteServeReposFromEnv()
    -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> repos{};
    auto* serve_repos = std::getenv("SERVE_REPOSITORIES");
    if (serve_repos not_eq nullptr) {
        std::istringstream pss(std::string{serve_repos});
        std::string path;
        while (std::getline(pss, path, ';')) {
            repos.emplace_back(path);
        }
    }
    return repos;
}

#endif  // INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP
