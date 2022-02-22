#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_CONFIG_HPP

#include <mutex>
#include <vector>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink.hpp"

/// \brief Global static logging configuration.
/// The entire class is thread-safe.
class LogConfig {
  public:
    /// \brief Set the log limit.
    static void SetLogLimit(LogLevel level) noexcept { log_limit_ = level; }

    /// \brief Replace all configured sinks.
    /// NOTE: Reinitializes all internal factories.
    static void SetSinks(std::vector<LogSinkFactory>&& factories) noexcept {
        std::lock_guard lock{mutex_};
        sinks_.clear();
        sinks_.reserve(factories.size());
        std::transform(factories.cbegin(),
                       factories.cend(),
                       std::back_inserter(sinks_),
                       [](auto& f) { return f(); });
        factories_ = std::move(factories);
    }

    /// \brief Add new a new sink.
    static void AddSink(LogSinkFactory&& factory) noexcept {
        std::lock_guard lock{mutex_};
        sinks_.push_back(factory());
        factories_.push_back(std::move(factory));
    }

    /// \brief Get the currently configured log limit.
    [[nodiscard]] static auto LogLimit() noexcept -> LogLevel {
        return log_limit_;
    }

    /// \brief Get sink instances for all configured sink factories.
    /// Returns a const copy of shared_ptrs, so accessing the sinks in the
    /// calling context is thread-safe.
    // NOLINTNEXTLINE(readability-const-return-type)
    [[nodiscard]] static auto Sinks() noexcept
        -> std::vector<ILogSink::Ptr> const {
        std::lock_guard lock{mutex_};
        return sinks_;
    }

    /// \brief Get all configured sink factories.
    /// Returns a const copy of shared_ptrs, so accessing the factories in the
    /// calling context is thread-safe.
    // NOLINTNEXTLINE(readability-const-return-type)
    [[nodiscard]] static auto SinkFactories() noexcept
        -> std::vector<LogSinkFactory> const {
        std::lock_guard lock{mutex_};
        return factories_;
    }

  private:
    static inline std::mutex mutex_{};
    static inline LogLevel log_limit_{LogLevel::Info};
    static inline std::vector<ILogSink::Ptr> sinks_{};
    static inline std::vector<LogSinkFactory> factories_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_CONFIG_HPP
