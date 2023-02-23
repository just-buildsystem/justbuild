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

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/progress_reporting/task_tracker.hpp"

class Progress {
  public:
    [[nodiscard]] static auto Instance() noexcept -> Progress& {
        static Progress instance{};
        return instance;
    }

    [[nodiscard]] auto TaskTracker() noexcept -> TaskTracker& {
        return task_tracker_;
    }

    // Return a reference to the origin map. It is the responsibility of the
    // caller to ensure that access only happens in a single-threaded context.
    [[nodiscard]] auto OriginMap() noexcept -> std::unordered_map<
        std::string,
        std::vector<
            std::pair<BuildMaps::Target::ConfiguredTarget, std::size_t>>>& {
        return origin_map_;
    }

  private:
    ::TaskTracker task_tracker_{};
    std::unordered_map<
        std::string,
        std::vector<
            std::pair<BuildMaps::Target::ConfiguredTarget, std::size_t>>>
        origin_map_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_HPP
