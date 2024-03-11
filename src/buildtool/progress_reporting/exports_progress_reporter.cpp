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

#include "src/buildtool/progress_reporting/exports_progress_reporter.hpp"

#include <string>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto ExportsProgressReporter::Reporter(gsl::not_null<Statistics*> const& stats,
                                       gsl::not_null<Progress*> const& progress,
                                       bool has_serve) noexcept
    -> progress_reporter_t {
    return BaseProgressReporter::Reporter([stats, progress, has_serve]() {
        // get 'found' counter last to ensure we never undercount the amount of
        // work not yet done
        auto cached = stats->ExportsCachedCounter();
        auto served = stats->ExportsServedCounter();
        auto uncached = stats->ExportsUncachedCounter();
        auto not_eligible = stats->ExportsNotEligibleCounter();
        auto found = stats->ExportsFoundCounter();

        auto active = progress->TaskTracker().Active();
        auto sample = progress->TaskTracker().Sample();

        auto msg = fmt::format(
            "Export targets: {} found [{} cached{}, {} analysed locally]",
            found,
            cached,
            has_serve ? fmt::format(", {} served", served) : "",
            uncached + not_eligible);

        if ((active > 0) && !sample.empty()) {
            msg = fmt::format("{} ({}{})",
                              msg,
                              nlohmann::json(sample).dump(),
                              active > 1 ? ", ..." : "");
        }
        Logger::Log(LogLevel::Progress, "{}", msg);
    });
}
