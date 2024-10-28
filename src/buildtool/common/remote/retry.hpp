// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_HPP

#ifndef BOOTSTRAP_BUILD_TOOL

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "grpcpp/grpcpp.h"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

// Utility class to help detecting when exit the retry loop. This class can be
// used when the failure cannot be immediately detected by the return value of
// the function. E.g., when using a grpc stream.
//
// Please note that it is user's responsibility to do not set both to true.
//
// Design note: even though only one bool could be sufficient (e.g., exit), this
// would require to check two times if we exited because of a success or a
// failure: the first time, inside the retry loop; the second time, by the
// caller.
struct RetryResponse {
    // When set to true, it means the function successfully run
    bool ok = false;
    // When set to true, it means that it is not worthy to retry.
    bool exit_retry_loop = false;
    // error message logged when exit_retry_loop was set to true or when the
    // last retry attempt failed
    std::optional<std::string> error_msg = std::nullopt;
};

using CallableReturningRetryResponse = std::function<RetryResponse(void)>;

/// \brief Calls a function with a retry strategy using a backoff algorithm.
/// Retry loop interrupts when one of the two members of the function's returned
/// RetryResponse object is set to true.
[[nodiscard]] auto WithRetry(
    CallableReturningRetryResponse const& f,
    RetryConfig const& retry_config,
    Logger const& logger,
    LogLevel fatal_log_level = LogLevel::Error) noexcept -> bool;

using CallableReturningGrpcStatus = std::function<grpc::Status(void)>;

/// \brief Calls a function with a retry strategy using a backoff algorithm.
/// Retry loop interrupts when function returns an error code different from
/// UNAVAILABLE.
[[nodiscard]] auto WithRetry(CallableReturningGrpcStatus const& f,
                             RetryConfig const& retry_config,
                             Logger const& logger) noexcept
    -> std::pair<bool, grpc::Status>;

#endif  // BOOTSTRAP_BUILD_TOOL
#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_HPP
