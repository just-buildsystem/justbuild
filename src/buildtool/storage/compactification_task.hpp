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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_COMPACTIFICATION_TASK_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_COMPACTIFICATION_TASK_HPP

#include <filesystem>
#include <functional>
#include <string>

#include "fmt/core.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/local_cas.hpp"

/// \brief Set of data that defines a compactification task.
/// \param cas          CAS to be scanned.
/// \param large        True if large storages need to be scanned.
/// \param f_task       A handler for ObjectType::File entries.
/// \param x_task       A handler for ObjectType::Executable entries.
/// It is never called during scanning of large storages.
/// \param t_task       A handler for ObjectType::Tree entries.
/// \param logger       A callback for logging.
/// \param filter       A callback that defines which files must be processed.
struct CompactificationTask final {
    using Logger = std::function<void(LogLevel, std::string const&)>;
    using Filter = std::function<bool(std::filesystem::path const&)>;
    using ObjectTask = std::function<bool(CompactificationTask const&,
                                          std::filesystem::path const&)>;

    static inline const Logger kLoggerDefault = [](LogLevel level,
                                                   std::string const& message) {
        ::Logger::Log(level, "{}", message);
    };
    static inline const Filter kFilterDefault =
        [](std::filesystem::path const& /*unused*/) { return false; };
    static inline const ObjectTask kObjectTaskDefault =
        [](CompactificationTask const& /*unused*/,
           std::filesystem::path const& /*unused*/) {
            kLoggerDefault(LogLevel::Error, "Default ObjectTask was called");
            return false;
        };

    LocalCAS<false> const& cas;
    bool large = false;
    Logger logger = kLoggerDefault;
    Filter filter = kFilterDefault;
    ObjectTask f_task = kObjectTaskDefault;
    ObjectTask x_task = kObjectTaskDefault;
    ObjectTask t_task = kObjectTaskDefault;

    /// \brief Log a formatted error.
    template <typename... Args>
    void Log(LogLevel level,
             std::string const& message,
             Args&&... args) const noexcept;
};

/// \brief Execute the comapctification task using multiple threads.
/// Execution of the CompactificationTask-defined logic begins only after the
/// entire storage has been scanned, otherwise the storage may be invalidated.
/// Example: casf is scanned while another thread puts new split chunks there.
/// \return         True if execution was successful.
[[nodiscard]] auto CompactifyConcurrently(
    CompactificationTask const& task) noexcept -> bool;

template <typename... Args>
void CompactificationTask::Log(
    LogLevel level,
    std::string const& message,
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    Args&&... args) const noexcept {
    if (not logger) {
        ::Logger::Log(LogLevel::Error, "Logger is missing.");
        return;
    }
    auto msg = fmt::vformat(message, fmt::make_format_args(args...));
    try {
        std::invoke(logger, level, msg);
    } catch (...) {
        ::Logger::Log(LogLevel::Error, "Failed to invoke a logger");
        ::Logger::Log(level, "{}", msg);
    }
}

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_COMPACTIFICATION_TASK_HPP
