#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP

#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "fmt/color.h"
#include "fmt/core.h"
#include "src/buildtool/logging/log_sink.hpp"
#include "src/buildtool/logging/logger.hpp"

class LogSinkCmdLine final : public ILogSink {
  public:
    static auto CreateFactory(bool colored = true) -> LogSinkFactory {
        return [=]() { return std::make_shared<LogSinkCmdLine>(colored); };
    }

    explicit LogSinkCmdLine(bool colored) noexcept : colored_{colored} {}
    ~LogSinkCmdLine() noexcept final = default;
    LogSinkCmdLine(LogSinkCmdLine const&) noexcept = delete;
    LogSinkCmdLine(LogSinkCmdLine&&) noexcept = delete;
    auto operator=(LogSinkCmdLine const&) noexcept -> LogSinkCmdLine& = delete;
    auto operator=(LogSinkCmdLine&&) noexcept -> LogSinkCmdLine& = delete;

    /// \brief Thread-safe emitting of log messages to stderr.
    void Emit(Logger const* logger,
              LogLevel level,
              std::string const& msg) const noexcept final {
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
            std::lock_guard lock{mutex_};
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
    static inline std::mutex mutex_{};

    [[nodiscard]] auto FormatPrefix(LogLevel level,
                                    std::string const& prefix) const noexcept
        -> std::string {
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
                case LogLevel::Debug:
                    style = fg(fmt::color::yellow);
                    break;
                case LogLevel::Trace:
                    style = fg(fmt::color::light_sky_blue);
                    break;
            }
        }
        return fmt::format(style, "{}", prefix);
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP
