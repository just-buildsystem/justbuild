#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP

#include <functional>
#include <istream>
#include <memory>
#include <string>

#include "src/buildtool/logging/log_level.hpp"

// forward declaration
class Logger;

class ILogSink {
  public:
    using Ptr = std::shared_ptr<ILogSink>;
    ILogSink() noexcept = default;
    ILogSink(ILogSink const&) = delete;
    ILogSink(ILogSink&&) = delete;
    auto operator=(ILogSink const&) -> ILogSink& = delete;
    auto operator=(ILogSink &&) -> ILogSink& = delete;
    virtual ~ILogSink() noexcept = default;

    /// \brief Thread-safe emitting of log messages.
    /// Logger might be 'nullptr' if called from the global context.
    virtual void Emit(Logger const* logger,
                      LogLevel level,
                      std::string const& msg) const noexcept = 0;

  protected:
    /// \brief Helper class for line iteration with std::istream_iterator.
    class Line : public std::string {
        friend auto operator>>(std::istream& is, Line& line) -> std::istream& {
            return std::getline(is, line);
        }
    };
};

using LogSinkFactory = std::function<ILogSink::Ptr()>;

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP
