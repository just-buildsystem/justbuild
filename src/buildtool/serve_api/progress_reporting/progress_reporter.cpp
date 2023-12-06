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

#include "src/buildtool/serve_api/progress_reporting/progress_reporter.hpp"

#include <string>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/progress_reporting/progress.hpp"
#include "src/buildtool/serve_api/progress_reporting/statistics.hpp"

auto ServeServiceProgressReporter::Reporter() noexcept -> progress_reporter_t {
    return BaseProgressReporter::Reporter([]() {
        auto const& stats = ServeServiceStatistics::Instance();
        // get served counter before dispatched counter, to ensure we never
        // undercount the amount of builds in flight
        int served = stats.ServedCounter();
        int dispatched = stats.DispatchedCounter();
        int processing = dispatched - served;
        int cached = stats.CacheHitsCounter();

        auto active = ServeServiceProgress::Instance().TaskTracker().Active();
        auto sample = ServeServiceProgress::Instance().TaskTracker().Sample();

        auto msg = fmt::format(
            "{} cached, {} served, {} processing", cached, served, processing);

        if ((active > 0) && !sample.empty()) {
            msg = fmt::format("{} ({}{})",
                              msg,
                              nlohmann::json(sample).dump(),
                              active > 1 ? ", ..." : "");
        }
        Logger::Log(LogLevel::Progress, "{}", msg);
    });
}
