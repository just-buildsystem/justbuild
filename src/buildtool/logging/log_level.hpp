#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_LEVEL_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_LEVEL_HPP

#include <algorithm>
#include <string>
#include <type_traits>

enum class LogLevel {
    Error,    ///< Error messages, fatal errors
    Warning,  ///< Warning messages, recoverable situations that shouldn't occur
    Info,     ///< Informative messages, such as reporting status or statistics
    Progress,  ///< Information about the current progress of the build
    Debug,     ///< Debug messages, such as details from internal processes
    Trace      ///< Trace messages, verbose details such as function calls
};

constexpr auto kFirstLogLevel = LogLevel::Error;
constexpr auto kLastLogLevel = LogLevel::Trace;

[[nodiscard]] static inline auto ToLogLevel(
    std::underlying_type_t<LogLevel> level) -> LogLevel {
    return std::min(std::max(static_cast<LogLevel>(level), kFirstLogLevel),
                    kLastLogLevel);
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
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Trace:
            return "TRACE";
    }
}

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_LEVEL_HPP
