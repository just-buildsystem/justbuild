// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/computed_roots/roots_progress.hpp"

#include <functional>
#include <string>

#include "fmt/core.h"
#include "src/buildtool/logging/log_level.hpp"

auto RootsProgress::Reporter(gsl::not_null<Statistics*> const& stats,
                             gsl::not_null<TaskTracker*> const& tasks,
                             Logger const* logger) noexcept
    -> progress_reporter_t {
    return BaseProgressReporter::Reporter([stats, tasks, logger]() {
        auto const& sample = tasks->Sample();
        // Note: order matters; queued has to be queried last
        int cached = stats->ActionsCachedCounter();
        int run = stats->ActionsExecutedCounter();
        int served = stats->ExportsServedCounter();
        int queued = stats->ActionsQueuedCounter();
        int active = queued - run - cached - served;
        auto now_msg = std::string{};
        if (active > 0 and not sample.empty()) {
            now_msg = fmt::format(" ({}{})", sample, active > 1 ? ", ..." : "");
        }
        Logger::Log(
            logger,
            LogLevel::Progress,
            "Computed Roots: {} cached, {} served, {} run, {} processing{}.",
            cached,
            served,
            run,
            active,
            now_msg);
    });
}
