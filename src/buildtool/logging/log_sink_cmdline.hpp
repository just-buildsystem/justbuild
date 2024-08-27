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

#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP

#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include "fmt/color.h"
#include "fmt/core.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink.hpp"
#include "src/buildtool/logging/logger.hpp"

class LogSinkCmdLine final : public ILogSink {
  public:
    static auto CreateFactory(bool colored = true,
                              std::optional<LogLevel> restrict_level =
                                  std::nullopt) -> LogSinkFactory {
        return [=]() {
            return std::make_shared<LogSinkCmdLine>(colored, restrict_level);
        };
    }

    explicit LogSinkCmdLine(bool colored,
                            std::optional<LogLevel> restrict_level) noexcept
        : colored_{colored}, restrict_level_{restrict_level} {}
    ~LogSinkCmdLine() noexcept final = default;
    LogSinkCmdLine(LogSinkCmdLine const&) noexcept = delete;
    LogSinkCmdLine(LogSinkCmdLine&&) noexcept = delete;
    auto operator=(LogSinkCmdLine const&) noexcept -> LogSinkCmdLine& = delete;
    auto operator=(LogSinkCmdLine&&) noexcept -> LogSinkCmdLine& = delete;

    /// \brief Thread-safe emitting of log messages to stderr.
    void Emit(Logger const* logger,
              LogLevel level,
              std::string const& msg) const noexcept final {
        static std::mutex mutex{};

        if (restrict_level_ and
            (static_cast<int>(*restrict_level_) < static_cast<int>(level))) {
            return;
        }

        auto prefix = LogLevelToString(level);

        if (logger != nullptr) {
            // append logger name
            prefix = fmt::format("{} ({})", prefix, logger->Name());
        }
        prefix = prefix + ":";
        auto cont_prefix = std::string(prefix.size(), ' ');
        prefix = FormatPrefix(level, prefix);
        bool msg_on_continuation{false};
        if (logger != nullptr and msg.find('\n') != std::string::npos) {
            cont_prefix = "    ";
            msg_on_continuation = true;
        }

        {
            std::lock_guard lock{mutex};
            if (msg_on_continuation) {
                fmt::print(stderr, "{}\n", prefix);
                prefix = cont_prefix;
            }
            using it = std::istream_iterator<ILogSink::Line>;
            std::istringstream iss{msg};
            for_each(it{iss}, it{}, [&](auto const& line) {
                fmt::print(stderr, "{} {}\n", prefix, line);
                prefix = cont_prefix;
            });
            std::fflush(stderr);
        }
    }

  private:
    bool colored_{};
    std::optional<LogLevel> restrict_level_{};

    [[nodiscard]] auto FormatPrefix(LogLevel level, std::string const& prefix)
        const noexcept -> std::string {
        fmt::text_style style{};
        if (colored_) {
            switch (level) {
                case LogLevel::Error:
                    style = fg(fmt::color::red);
                    break;
                case LogLevel::Warning:
                    style = fg(fmt::color::orange);
                    break;
                case LogLevel::Info:
                    style = fg(fmt::color::lime_green);
                    break;
                case LogLevel::Progress:
                    style = fg(fmt::color::dark_green);
                    break;
                case LogLevel::Performance:
                    style = fg(fmt::color::light_sky_blue);
                    break;
                case LogLevel::Debug:
                    style = fg(fmt::color::sky_blue);
                    break;
                case LogLevel::Trace:
                    style = fg(fmt::color::deep_sky_blue);
                    break;
            }
        }
        try {
            return fmt::format(style, "{}", prefix);
        } catch (...) {
            return prefix;
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP
