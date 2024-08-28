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

#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_UTILS_HPP

#include <functional>
#include <optional>
#include <sstream>
#include <string>

#include "fmt/core.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

/// \brief Utility to detect and report cycles for an AsyncMap instance.
/// \param name Human-readable string identifier related to the map or its use.
/// \param map The AsyncMap instance.
/// \param key_printer Callable returning key-specific identifier in string
/// format.
/// \returns The resulting cycle message as a string, or nullopt if no cycle
/// detected.
template <typename K, typename V>
[[nodiscard]] auto DetectAndReportCycle(
    std::string const& name,
    AsyncMapConsumer<K, V> const& map,
    std::function<std::string(K const&)> key_printer)
    -> std::optional<std::string> {
    using namespace std::string_literals;
    auto cycle = map.DetectCycle();
    if (cycle) {
        bool found{false};
        std::ostringstream oss{};
        oss << fmt::format("Cycle detected in {}:", name) << std::endl;
        for (auto const& k : *cycle) {
            auto match = (k == cycle->back());
            auto prefix{match   ? found ? "`-- "s : ".-> "s
                        : found ? "|   "s
                                : "    "s};
            oss << prefix << key_printer(k) << std::endl;
            found = found or match;
        }
        return oss.str();
    }
    return std::nullopt;
}

/// \brief Utility to detect and report pending tasks for an AsyncMap instance.
/// \param name Human-readable string identifier related to the map or its use.
/// \param map The AsyncMap instance.
/// \param key_printer Callable returning key-specific identifier in string
/// format.
/// \param logger Named logger, or nullptr to use global logger.
template <typename K, typename V>
void DetectAndReportPending(std::string const& name,
                            AsyncMapConsumer<K, V> const& map,
                            std::function<std::string(K const&)> key_printer,
                            Logger const* logger = nullptr) {
    using namespace std::string_literals;
    auto keys = map.GetPendingKeys();
    if (not keys.empty()) {
        std::ostringstream oss{};
        oss << fmt::format("Internal error, failed to evaluate pending {}:",
                           name)
            << std::endl;
        for (auto const& k : keys) {
            oss << "  " << key_printer(k) << std::endl;
        }
        Logger::Log(logger, LogLevel::Error, "{}", oss.str());
    }
}

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_UTILS_HPP
