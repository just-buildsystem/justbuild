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

#include <optional>
#include <thread>

#include "grpcpp/grpcpp.h"
#include "src/buildtool/common/remote/retry_parameters.hpp"
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
    bool ok{false};
    // When set to true, it means that it is not worthy to retry.
    bool exit_retry_loop{false};
    // error message logged when exit_retry_loop was set to true or when the
    // last retry attempt failed
    std::optional<std::string> error_msg{std::nullopt};
};

template <typename F>
concept CallableReturningRetryResponse = requires(F const& f) {
    {RetryResponse{f()}};
};

template <CallableReturningRetryResponse F>
// \p f is the callable invoked with a back off algorithm. The retry loop is
// interrupted when one of the two member of the returned RetryResponse object
// is set to true.
[[nodiscard]] auto WithRetry(F const& f, Logger const& logger) noexcept
    -> bool {
    try {
        auto const& attempts = Retry::GetMaxAttempts();
        for (auto attempt = 1U; attempt <= attempts; ++attempt) {
            auto [ok, fatal, error_msg] = f();
            if (ok) {
                return true;
            }
            if (fatal) {
                if (error_msg) {
                    logger.Emit(LogLevel::Error, *error_msg);
                }
                return false;
            }
            // don't wait if it was the last attempt
            if (attempt < attempts) {
                auto const sleep_for_seconds =
                    Retry::GetSleepTimeSeconds(attempt);
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
                    logger.Emit(LogLevel::Error,
                                "After {} attempts: {}",
                                attempt,
                                *error_msg);
                }
            }
        }
    } catch (...) {
        logger.Emit(LogLevel::Error, "WithRetry: caught unknown exception");
    }
    return false;
}

template <typename F>
concept CallableReturningGrpcStatus = requires(F const& f) {
    {grpc::Status{f()}};
};

template <CallableReturningGrpcStatus F>
// F is the function to be invoked with a back off algorithm
[[nodiscard]] auto WithRetry(F const& f, Logger const& logger) noexcept
    -> std::pair<bool, grpc::Status> {
    grpc::Status status{};
    try {
        auto attempts = Retry::GetMaxAttempts();
        for (auto attempt = 1U; attempt <= attempts; ++attempt) {
            status = f();
            if (status.ok() or
                status.error_code() != grpc::StatusCode::UNAVAILABLE) {
                return {status.ok(), std::move(status)};
            }
            // don't wait if it was the last attempt
            if (attempt < attempts) {
                auto const sleep_for_seconds =
                    Retry::GetSleepTimeSeconds(attempt);
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
