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

#include "src/other_tools/just_mr/progress_reporting/progress_reporter.hpp"

#include <functional>
#include <string>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/task_tracker.hpp"

auto JustMRProgressReporter::Reporter(
    gsl::not_null<JustMRStatistics*> const& stats,
    gsl::not_null<JustMRProgress*> const& progress) noexcept
    -> progress_reporter_t {
    return BaseProgressReporter::Reporter([stats, progress]() {
        auto const total = progress->GetTotal();
        auto const local = stats->LocalPathsCounter();
        auto const cached = stats->CacheHitsCounter();
        auto const computed = stats->ComputedCounter();
        auto const run = stats->ExecutedCounter();
        auto const active = progress->TaskTracker().Active();
        auto const sample = progress->TaskTracker().Sample();
        auto msg = fmt::format("{} comptued, {} local, {} cached, {} done",
                               computed,
                               local,
                               cached,
                               run);
        if ((active > 0) and not sample.empty()) {
            msg = fmt::format("{}; {} fetches ({}{})",
                              msg,
                              active,
                              nlohmann::json(sample).dump(),
                              active > 1 ? ", ..." : "");
        }
        constexpr int kOneHundred{100};
        int progress = kOneHundred;  // default if no work has to be done
        if (auto const noops = cached + local + computed; noops < total) {
            progress = static_cast<int>(run * kOneHundred / (total - noops));
        }
        Logger::Log(LogLevel::Progress, "[{:3}%] {}", progress, msg);
    });
}
