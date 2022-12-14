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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_HPP

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/logging/logger.hpp"

class Progress {
  public:
    [[nodiscard]] static auto Instance() noexcept -> Progress& {
        static Progress instance{};
        return instance;
    }

    void Start(const std::string& id) noexcept {
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
    void Stop(const std::string& id) noexcept {
        std::unique_lock lock(m_);
        running_.erase(id);
    }

    auto Sample() -> std::string {
        std::unique_lock lock(m_);
        std::string result;
        uint64_t started = prio_ + 1;
        for (auto const& it : running_) {
            if (it.second < started) {
                result = it.first;
                started = it.second;
            }
        }
        return result;
    }

    // Return a reference to the origin map. It is the responsibility
    // of the caller to ensure that access only happens in a
    // single-threaded context.
    auto OriginMap() -> std::unordered_map<
        std::string,
        std::vector<
            std::pair<BuildMaps::Target::ConfiguredTarget, std::size_t>>>& {
        return origin_map_;
    }

  private:
    uint64_t prio_{};
    std::mutex m_{};
    std::unordered_map<std::string, uint64_t> running_{};
    std::unordered_map<
        std::string,
        std::vector<
            std::pair<BuildMaps::Target::ConfiguredTarget, std::size_t>>>
        origin_map_;
};

#endif
