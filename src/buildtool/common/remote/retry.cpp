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

#include "src/buildtool/common/remote/retry.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL

#include <algorithm>
#include <chrono>
#include <thread>

#include "fmt/core.h"
#include "gsl/gsl"

auto WithRetry(CallableReturningRetryResponse const& f,
               RetryConfig const& retry_config,
               Logger const& logger,
               LogLevel fatal_log_level) noexcept -> bool {
    try {
        auto const& attempts = retry_config.GetMaxAttempts();
        for (auto attempt = 1U; attempt <= attempts; ++attempt) {
            auto [ok, fatal, error_msg] = f();
            if (ok) {
                return true;
            }
            if (fatal) {
                if (error_msg) {
                    logger.Emit(fatal_log_level, "{}", *error_msg);
                }
                return false;
            }
            // don't wait if it was the last attempt
            if (attempt < attempts) {
                auto const sleep_for_seconds =
                    retry_config.GetSleepTimeSeconds(attempt);
                logger.Emit(kRetryLogLevel,
                            "Attempt {}/{} failed{} Retrying in {} seconds.",
                            attempt,
                            attempts,
                            error_msg ? fmt::format(": {}", *error_msg) : ".",
                            sleep_for_seconds);
                std::this_thread::sleep_for(
                    std::chrono::seconds(sleep_for_seconds));
            }
            else {
                if (error_msg) {
                    logger.Emit(fatal_log_level,
                                "After {} attempts: {}",
                                attempt,
                                *error_msg);
                }
            }
        }
    } catch (...) {
        logger.Emit(std::min(fatal_log_level, LogLevel::Warning),
                    "WithRetry: caught unknown exception");
    }
    return false;
}
auto WithRetry(CallableReturningGrpcStatus const& f,
               RetryConfig const& retry_config,
               Logger const& logger) noexcept -> std::pair<bool, grpc::Status> {
    grpc::Status status{};
    try {
        auto attempts = retry_config.GetMaxAttempts();
        for (auto attempt = 1U; attempt <= attempts; ++attempt) {
            status = f();
            if (status.ok() or
                ((status.error_code() != grpc::StatusCode::UNAVAILABLE) and
                 (status.error_code() !=
                  grpc::StatusCode::DEADLINE_EXCEEDED))) {
                return {status.ok(), std::move(status)};
            }
            // don't wait if it was the last attempt
            if (attempt < attempts) {
                auto const sleep_for_seconds =
                    retry_config.GetSleepTimeSeconds(attempt);
                logger.Emit(
                    kRetryLogLevel,
                    "Attempt {}/{} failed: {}: {}: Retrying in {} seconds.",
                    attempt,
                    attempts,
                    static_cast<int>(status.error_code()),
                    status.error_message(),
                    sleep_for_seconds);
                std::this_thread::sleep_for(
                    std::chrono::seconds(sleep_for_seconds));
            }
            else {
                // The caller performs a second check on the
                // status.error_code(), and, eventually, emits to Error level
                // there.
                //
                // To avoid duplication of similar errors, we emit to Debug
                // level.
                logger.Emit(LogLevel::Debug,
                            "After {} attempts: {}: {}",
                            attempt,
                            static_cast<int>(status.error_code()),
                            status.error_message());
            }
        }
    } catch (...) {
        logger.Emit(LogLevel::Error, "WithRetry: caught unknown exception");
    }
    return {false, std::move(status)};
}

[[nodiscard]] auto IsReasonableToRetry(grpc::Status const& status) noexcept
    -> bool {
    // NOLINTBEGIN(bugprone-branch-clone,-warnings-as-errors)
    switch (status.error_code()) {
        case grpc::StatusCode::OK: {
            // Success, don't retry
            return false;
        }
        case grpc::StatusCode::CANCELLED: {
            // Operation canceled by the user, don't retry
            return false;
        }
        case grpc::StatusCode::UNKNOWN: {
            // Errors raised by APIs that do not return enough error information
            // may be converted to this error, don't retry
            return false;
        }
        case grpc::StatusCode::INVALID_ARGUMENT: {
            // Client specified an invalid argument, don't retry
            return false;
        }
        case grpc::StatusCode::DEADLINE_EXCEEDED: {
            // Deadline expired before operation could complete, retry
            return true;
        }
        case grpc::StatusCode::NOT_FOUND: {
            // Requested entity was not found, don't retry
            return false;
        }
        case grpc::StatusCode::ALREADY_EXISTS: {
            // Entity that we attempted to create (e.g., file or directory)
            // already exists, don't retry
            return false;
        }
        case grpc::StatusCode::PERMISSION_DENIED: {
            // The caller does not have permission to execute the specified
            // operation, don't retry
            return false;
        }
        case grpc::StatusCode::UNAUTHENTICATED: {
            // The request does not have valid authentication credentials, don't
            // retry
            return false;
        }
        case grpc::StatusCode::RESOURCE_EXHAUSTED: {
            // Some resource has been exhausted, perhaps a per-user quota, or
            // perhaps the entire file system is out of space, retry:
            return true;
        }
        case grpc::StatusCode::FAILED_PRECONDITION: {
            // Client performs conditional REST operation on a resource and the
            // resource on the server does not match the condition, don't retry
            return false;
        }
        case grpc::StatusCode::ABORTED: {
            // Client should retry at a higher-level, don't retry
            return false;
        }
        case grpc::StatusCode::OUT_OF_RANGE: {
            // Operation was attempted past the valid range. E.g., seeking or
            // reading past end of file. Unlike INVALID_ARGUMENT, this error
            // indicates a problem that may be fixed if the system state
            // changes.
            return true;
        }
        case grpc::StatusCode::UNIMPLEMENTED: {
            // Operation is not implemented or not supported/enabled in this
            // service, don't retry
            return false;
        }
        case grpc::StatusCode::INTERNAL: {
            // Something is very broken, don't retry
            return false;
        }
        case grpc::StatusCode::UNAVAILABLE: {
            // The service is currently unavailable, retry:
            return true;
        }
        case grpc::StatusCode::DATA_LOSS: {
            // Unrecoverable data loss or corruption, retry:
            return true;
        }
        case grpc::StatusCode::DO_NOT_USE: {
            // "Force users to include a default branch", but we want a compile
            // error if a new case appears, so check DO_NOT_USE explicitly
            return false;
        }
    }
    // NOLINTEND(bugprone-branch-clone,-warnings-as-errors)
    Expects(false);  // unreachable
}

#endif  // BOOTSTRAP_BUILD_TOOL
