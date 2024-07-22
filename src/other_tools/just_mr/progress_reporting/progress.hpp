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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_HPP

#include <cstddef>

#include "src/buildtool/progress_reporting/task_tracker.hpp"

class JustMRProgress final {
  public:
    explicit JustMRProgress(std::size_t total) noexcept : total_{total} {};

    [[nodiscard]] auto TaskTracker() noexcept -> TaskTracker& {
        return task_tracker_;
    }

    [[nodiscard]] auto GetTotal() const noexcept -> std::size_t {
        return total_;
    }

  private:
    ::TaskTracker task_tracker_{};
    std::size_t total_ = 0;
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_HPP
