// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_TRACKER_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_TRACKER_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

class TaskTracker {
  public:
    auto Start(const std::string& id) noexcept -> void {
        std::unique_lock lock(m_);
        ++prio_;
        try {
            running_.emplace(id, prio_);
        } catch (...) {
            Logger::Log(LogLevel::Warning,
                        "Internal error in progress tracking; progress reports "
                        "might be incorrect.");
        }
    }

    auto Stop(const std::string& id) noexcept -> void {
        std::unique_lock lock(m_);
        running_.erase(id);
    }

    [[nodiscard]] auto Sample() noexcept -> std::string {
        std::unique_lock lock(m_);
        std::string result{};
        uint64_t started = prio_ + 1;
        for (auto const& it : running_) {
            if (it.second < started) {
                result = it.first;
                started = it.second;
            }
        }
        return result;
    }

    [[nodiscard]] auto Active() noexcept -> std::size_t {
        std::unique_lock lock(m_);
        return running_.size();
    }

  private:
    uint64_t prio_{};
    std::mutex m_{};
    std::unordered_map<std::string, uint64_t> running_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_TRACKER_HPP
