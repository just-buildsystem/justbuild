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

#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOGGER_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOGGER_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_sink.hpp"

class Logger {
  public:
    using MessageCreateFunc = std::function<std::string()>;

    /// \brief Create logger with sink instances from LogConfig::Sinks().
    explicit Logger(std::string name) noexcept
        : name_{std::move(name)},
          log_limit_{LogConfig::LogLimit()},
          sinks_{LogConfig::Sinks()} {}

    /// \brief Create logger with new sink instances from specified factories.
    Logger(std::string name,
           std::vector<LogSinkFactory> const& factories) noexcept
        : name_{std::move(name)}, log_limit_{LogConfig::LogLimit()} {
        sinks_.reserve(factories.size());
        std::transform(factories.cbegin(),
                       factories.cend(),
                       std::back_inserter(sinks_),
                       [](auto& f) { return f(); });
    }

    ~Logger() noexcept = default;
    Logger(Logger const&) noexcept = delete;
    Logger(Logger&&) noexcept = delete;
    auto operator=(Logger const&) noexcept -> Logger& = delete;
    auto operator=(Logger&&) noexcept -> Logger& = delete;

    /// \brief Get logger name.
    [[nodiscard]] auto Name() const& noexcept -> std::string const& {
        return name_;
    }

    /// \brief Get log limit.
    [[nodiscard]] auto LogLimit() const noexcept -> LogLevel {
        return log_limit_;
    }

    /// \brief Set log limit.
    void SetLogLimit(LogLevel level) noexcept { log_limit_ = level; }

    /// \brief Emit log message from string via this logger instance.
    template <class... T_Args>
    void Emit(LogLevel level,
              std::string const& msg,
              T_Args&&... args) const noexcept {
        if (static_cast<int>(level) <= static_cast<int>(log_limit_)) {
            FormatAndForward(
                this, sinks_, level, msg, std::forward<T_Args>(args)...);
        }
    }

    /// \brief Emit log message from lambda via this logger instance.
    void Emit(LogLevel level,
              MessageCreateFunc const& msg_creator) const noexcept {
        if (static_cast<int>(level) <= static_cast<int>(log_limit_)) {
            FormatAndForward(this, sinks_, level, msg_creator());
        }
    }

    /// \brief Log message from string via LogConfig's sinks and log limit.
    template <class... T_Args>
    static void Log(LogLevel level,
                    std::string const& msg,
                    T_Args&&... args) noexcept {
        if (static_cast<int>(level) <=
            static_cast<int>(LogConfig::LogLimit())) {
            FormatAndForward(nullptr,
                             LogConfig::Sinks(),
                             level,
                             msg,
                             std::forward<T_Args>(args)...);
        }
    }

    /// \brief Log message from lambda via LogConfig's sinks and log limit.
    static void Log(LogLevel level,
                    MessageCreateFunc const& msg_creator) noexcept {
        if (static_cast<int>(level) <=
            static_cast<int>(LogConfig::LogLimit())) {
            FormatAndForward(nullptr, LogConfig::Sinks(), level, msg_creator());
        }
    }

  private:
    std::string name_{};
    LogLevel log_limit_{};
    std::vector<ILogSink::Ptr> sinks_{};

    /// \brief Format message and forward to sinks.
    template <class... T_Args>
    static void FormatAndForward(Logger const* logger,
                                 std::vector<ILogSink::Ptr> const& sinks,
                                 LogLevel level,
                                 std::string const& msg,
                                 T_Args&&... args) noexcept {
        if constexpr (sizeof...(T_Args) == 0) {
            // forward to sinks
            std::for_each(sinks.cbegin(), sinks.cend(), [&](auto& sink) {
                sink->Emit(logger, level, msg);
            });
        }
        else {
            // format the message
            auto fmsg = fmt::vformat(msg, fmt::make_format_args(args...));
            // recursive call without format arguments
            FormatAndForward(logger, sinks, level, fmsg);
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOGGER_HPP
