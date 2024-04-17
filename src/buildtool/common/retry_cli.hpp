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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_CLI_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_RETRY_CLI_HPP

#include <optional>

#include "CLI/CLI.hpp"
#include "gsl/gsl"

/// \brief Arguments required for tuning the retry strategy.
struct RetryArguments {
    std::optional<unsigned int> max_attempts{};
    std::optional<unsigned int> initial_backoff_seconds{};
    std::optional<unsigned int> max_backoff_seconds{};
};

static inline void SetupRetryArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<RetryArguments*> const& args) {
    app->add_option(
        "--max-attempts",
        args->max_attempts,
        "Total number of attempts in case of a remote resource becomes "
        "unavailable. Must be greater than 0. (Default: 1)");
    app->add_option(
        "--initial-backoff-seconds",
        args->initial_backoff_seconds,
        "Initial amount of time, in seconds, to wait before retrying a rpc "
        "call. The waiting time is doubled at each attempt. Must be greater "
        "than 0. (Default: 1)");
    app->add_option("--max-backoff-seconds",
                    args->max_backoff_seconds,
                    "The backoff time cannot be bigger than this parameter. "
                    "Note that some jitter is still added to avoid to overload "
                    "the resources that survived the outage. (Default: 60)");
}

#endif
