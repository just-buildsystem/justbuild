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

#include <optional>
#include <string>

#include "fmt/core.h"
#include "gsl-lite/gsl-lite.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"

auto JustMRProgressReporter::Reporter() noexcept -> progress_reporter_t {
    return BaseProgressReporter::Reporter([]() {
        int total =
            gsl::narrow<int>(JustMRProgress::Instance().RepositorySet().size());
        // Note: order of stats queries matters!
        auto const& sample = JustMRProgress::Instance().TaskTracker().Sample();
        auto const& stats = JustMRStatistics::Instance();
        int local = stats.LocalPathsCounter();
        int cached = stats.CacheHitsCounter();
        int run = stats.ExecutedCounter();
        int queued = stats.QueuedCounter();
        int active = queued - run;
        std::string msg;
        if (active > 0 and !sample.empty()) {
            auto const& repo_set = JustMRProgress::Instance().RepositorySet();
            if (repo_set.find(sample) != repo_set.end()) {
                msg = fmt::format(
                    "{} local roots, {} cached, {} run, {} processing "
                    "({}{})",
                    local,
                    cached,
                    run,
                    active,
                    nlohmann::json(sample).dump(),
                    active > 1 ? ", ..." : "");
            }
        }
        constexpr int kOneHundred{100};
        int total_work = total - cached - local;
        int progress = kOneHundred;  // default if no work has to be done
        if (total_work > 0) {
            progress = run * kOneHundred / total_work;
        }
        Logger::Log(LogLevel::Progress, "[{:3}%] {}", progress, msg);
    });
}
