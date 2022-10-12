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

#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_CONFIG_HPP

#include <mutex>
#include <vector>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink.hpp"

/// \brief Global static logging configuration.
/// The entire class is thread-safe.
class LogConfig {
    struct ConfigData {
        std::mutex mutex{};
        LogLevel log_limit{LogLevel::Info};
        std::vector<ILogSink::Ptr> sinks{};
        std::vector<LogSinkFactory> factories{};
    };

  public:
    /// \brief Set the log limit.
    static void SetLogLimit(LogLevel level) noexcept {
        Data().log_limit = level;
    }

    /// \brief Replace all configured sinks.
    /// NOTE: Reinitializes all internal factories.
    static void SetSinks(std::vector<LogSinkFactory>&& factories) noexcept {
        auto& data = Data();
        std::lock_guard lock{data.mutex};
        data.sinks.clear();
        data.sinks.reserve(factories.size());
        std::transform(factories.cbegin(),
                       factories.cend(),
                       std::back_inserter(data.sinks),
                       [](auto& f) { return f(); });
        data.factories = std::move(factories);
    }

    /// \brief Add new a new sink.
    static void AddSink(LogSinkFactory&& factory) noexcept {
        auto& data = Data();
        std::lock_guard lock{data.mutex};
        data.sinks.push_back(factory());
        data.factories.push_back(std::move(factory));
    }

    /// \brief Get the currently configured log limit.
    [[nodiscard]] static auto LogLimit() noexcept -> LogLevel {
        return Data().log_limit;
    }

    /// \brief Get sink instances for all configured sink factories.
    /// Returns a const copy of shared_ptrs, so accessing the sinks in the
    /// calling context is thread-safe.
    // NOLINTNEXTLINE(readability-const-return-type)
    [[nodiscard]] static auto Sinks() noexcept
        -> std::vector<ILogSink::Ptr> const {
        auto& data = Data();
        std::lock_guard lock{data.mutex};
        return data.sinks;
    }

    /// \brief Get all configured sink factories.
    /// Returns a const copy of shared_ptrs, so accessing the factories in the
    /// calling context is thread-safe.
    // NOLINTNEXTLINE(readability-const-return-type)
    [[nodiscard]] static auto SinkFactories() noexcept
        -> std::vector<LogSinkFactory> const {
        auto& data = Data();
        std::lock_guard lock{data.mutex};
        return data.factories;
    }

  private:
    [[nodiscard]] static auto Data() noexcept -> ConfigData& {
        static ConfigData instance{};
        return instance;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_CONFIG_HPP
