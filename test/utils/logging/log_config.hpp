#ifndef INCLUDED_SRC_TEST_UTILS_LOGGING_LOG_CONFIG_HPP
#define INCLUDED_SRC_TEST_UTILS_LOGGING_LOG_CONFIG_HPP

#include <cstdlib>

#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"

static auto ReadLogLevelFromEnv() -> LogLevel {
    LogLevel const kDefaultTestLogLevel{LogLevel::Error};
    LogLevel const kMaximumTestLogLevel{LogLevel::Trace};

    auto log_level{kDefaultTestLogLevel};

    auto* log_level_str = std::getenv("LOG_LEVEL_TESTS");
    if (log_level_str not_eq nullptr) {
        try {
            log_level = static_cast<LogLevel>(std::stoul(log_level_str));
        } catch (std::exception&) {
            log_level = kDefaultTestLogLevel;
        }
    }

    switch (log_level) {
        case LogLevel::Error:
        case LogLevel::Warning:
        case LogLevel::Info:
        case LogLevel::Progress:
        case LogLevel::Debug:
        case LogLevel::Trace:
            return log_level;
    }

    // log level is out of range
    return kMaximumTestLogLevel;
}

[[maybe_unused]] static inline void ConfigureLogging() {
    LogConfig::SetLogLimit(ReadLogLevelFromEnv());
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory(false /*no color*/)});
}

#endif  // INCLUDED_SRC_TEST_UTILS_LOGGING_LOG_CONFIG_HPP
