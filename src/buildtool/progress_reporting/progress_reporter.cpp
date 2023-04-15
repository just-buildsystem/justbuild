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

#include "src/buildtool/progress_reporting/progress_reporter.hpp"

#include <string>

#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"

auto ProgressReporter::Reporter() noexcept -> progress_reporter_t {
    return BaseProgressReporter::Reporter([]() {
        int total = gsl::narrow<int>(Progress::Instance().OriginMap().size());
        // Note: order matters; queued has to be queried last
        auto const& sample = Progress::Instance().TaskTracker().Sample();
        auto const& stats = Statistics::Instance();
        int cached = stats.ActionsCachedCounter();
        int run = stats.ActionsExecutedCounter();
        int queued = stats.ActionsQueuedCounter();
        int active = queued - run - cached;
        std::string now_msg;
        if (active > 0 and !sample.empty()) {
            auto const& origin_map = Progress::Instance().OriginMap();
            auto origins = origin_map.find(sample);
            if (origins != origin_map.end() and !origins->second.empty()) {
                auto const& origin = origins->second[0];
                now_msg = fmt::format(" ({}#{}{})",
                                      origin.first.target.ToString(),
                                      origin.second,
                                      active > 1 ? ", ..." : "");
            }
            else {
                now_msg =
                    fmt::format(" ({}{})", sample, active > 1 ? ", ..." : "");
            }
        }
        constexpr int kOneHundred{100};
        int total_work = total - cached;
        int progress = kOneHundred;  // default if no work has to be done
        if (total_work > 0) {
            progress = run * kOneHundred / total_work;
        }
        Logger::Log(LogLevel::Progress,
                    "[{:3}%] {} cached, {} run, {} processing{}.",
                    progress,
                    cached,
                    run,
                    active,
                    now_msg);
    });
}
