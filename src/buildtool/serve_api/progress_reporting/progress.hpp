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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_PROGRESS_REPORTING_PROGRESS_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_PROGRESS_REPORTING_PROGRESS_HPP

#include <cstdlib>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/buildtool/progress_reporting/task_tracker.hpp"

class ServeServiceProgress {
  public:
    [[nodiscard]] static auto Instance() noexcept -> ServeServiceProgress& {
        static ServeServiceProgress instance{};
        return instance;
    }

    [[nodiscard]] auto TaskTracker() noexcept -> TaskTracker& {
        return task_tracker_;
    }

  private:
    ::TaskTracker task_tracker_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_PROGRESS_REPORTING_PROGRESS_HPP
