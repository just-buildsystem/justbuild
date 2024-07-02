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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_CLIENT_COMMON_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_CLIENT_COMMON_HPP

/// \file client_common.hpp
/// \brief Common types and functions required by client implementations.

#include <optional>
#include <sstream>
#include <string>
#include <variant>

#include "fmt/core.h"
#include "grpcpp/grpcpp.h"
#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

[[maybe_unused]] [[nodiscard]] static inline auto CreateChannelWithCredentials(
    std::string const& server,
    Port port,
    gsl::not_null<Auth const*> const& auth) noexcept {

    std::shared_ptr<grpc::ChannelCredentials> creds;
    std::string address = server + ':' + std::to_string(port);
    if (const auto* tls_auth = std::get_if<Auth::TLS>(&auth->method);
        tls_auth != nullptr) {
        auto tls_opts = grpc::SslCredentialsOptions{
            tls_auth->ca_cert, tls_auth->client_key, tls_auth->client_cert};
        creds = grpc::SslCredentials(tls_opts);
    }
    else {
        // currently only TLS/SSL is supported
        creds = grpc::InsecureChannelCredentials();
    }
    return grpc::CreateChannel(address, creds);
}

[[nodiscard]] static inline auto StatusString(
    grpc::Status const& s,
    std::optional<std::string> const& prefix = std::nullopt) noexcept
    -> std::string {
    return fmt::format("{}{}: {}",
                       (prefix.has_value() ? fmt::format("{}: ", *prefix) : ""),
                       static_cast<int>(s.error_code()),
                       s.error_message());
}

[[nodiscard]] static inline auto StatusString(
    google::rpc::Status const& s,
    std::optional<std::string> const& prefix = std::nullopt) noexcept
    -> std::string {
    return fmt::format("{}{}: {}",
                       (prefix.has_value() ? fmt::format("{}: ", *prefix) : ""),
                       static_cast<int>(s.code()),
                       s.message());
}

template <typename T_Status>
[[maybe_unused]] static inline void LogStatus(
    Logger const* logger,
    LogLevel level,
    T_Status const& s,
    std::optional<std::string> const& prefix = std::nullopt) noexcept {
    auto msg = [&s, &prefix]() { return StatusString(s, prefix); };
    if (logger == nullptr) {
        Logger::Log(level, msg);
    }
    else {
        logger->Emit(level, msg);
    }
}

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_CLIENT_COMMON_HPP
