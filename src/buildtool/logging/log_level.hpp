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

#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_LEVEL_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_LEVEL_HPP

#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>

#include <gsl/gsl>

enum class LogLevel {
    Error,    ///< Error messages, fatal errors
    Warning,  ///< Warning messages, recoverable situations that shouldn't occur
    Info,     ///< Informative messages, such as reporting status or statistics
    Progress,     ///< Information about the current progress of the build
    Performance,  ///< Information about performance issues
    Debug,        ///< Debug messages, such as details from internal processes
    Trace         ///< Trace messages, verbose details such as function calls
};

constexpr auto kFirstLogLevel = LogLevel::Error;
constexpr auto kLastLogLevel = LogLevel::Trace;

[[nodiscard]] static inline auto ToLogLevel(
    std::underlying_type_t<LogLevel> level) -> LogLevel {
    return std::min(std::max(static_cast<LogLevel>(level), kFirstLogLevel),
                    kLastLogLevel);
}

[[nodiscard]] static inline auto ToLogLevel(double level) -> LogLevel {
    if (level < static_cast<double>(kFirstLogLevel)) {
        return kFirstLogLevel;
    }
    if (level > static_cast<double>(kLastLogLevel)) {
        return kLastLogLevel;
    }
    // Now we're in range, so we can round and cast.
    try {
        return ToLogLevel(
            static_cast<std::underlying_type_t<LogLevel>>(std::lround(level)));
    } catch (...) {
        // should not happen, but chose the most conservative value
        return kLastLogLevel;
    }
}

[[nodiscard]] static inline auto LogLevelToString(LogLevel level)
    -> std::string {
    switch (level) {
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Progress:
            return "PROG";
        case LogLevel::Performance:
            return "PERF";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Trace:
            return "TRACE";
    }
    Ensures(false);  // unreachable
}

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_LEVEL_HPP
