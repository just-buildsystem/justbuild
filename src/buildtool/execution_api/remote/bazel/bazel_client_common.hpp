#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CLIENT_COMMON_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CLIENT_COMMON_HPP

/// \file bazel_client_common.hpp
/// \brief Common types and functions required by client implementations.

#include <sstream>
#include <string>

#include "grpcpp/grpcpp.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/logging/logger.hpp"

[[maybe_unused]] [[nodiscard]] static inline auto CreateChannelWithCredentials(
    std::string const& server,
    Port port,
    std::string const& user = "",
    [[maybe_unused]] std::string const& pwd = "") noexcept {
    std::shared_ptr<grpc::ChannelCredentials> cred;
    std::string address = server + ':' + std::to_string(port);
    if (user.empty()) {
        cred = grpc::InsecureChannelCredentials();
    }
    else {
        // TODO(oreiche): set up authentication credentials
    }
    return grpc::CreateChannel(address, cred);
}

[[maybe_unused]] static inline void LogStatus(Logger const* logger,
                                              LogLevel level,
                                              grpc::Status const& s) noexcept {
    if (logger == nullptr) {
        Logger::Log(level, "{}: {}", s.error_code(), s.error_message());
    }
    else {
        logger->Emit(level, "{}: {}", s.error_code(), s.error_message());
    }
}

[[maybe_unused]] static inline void LogStatus(
    Logger const* logger,
    LogLevel level,
    google::rpc::Status const& s) noexcept {
    if (logger == nullptr) {
        Logger::Log(level, "{}: {}", s.code(), s.message());
    }
    else {
        logger->Emit(level, "{}: {}", s.code(), s.message());
    }
}

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CLIENT_COMMON_HPP
