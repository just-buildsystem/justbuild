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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_REPORTER_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_REPORTER_HPP

#include "gsl/gsl"
#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"

class JustMRProgressReporter final {
  public:
    [[nodiscard]] static auto Reporter(
        gsl::not_null<JustMRStatistics*> const& stats,
        gsl::not_null<JustMRProgress*> const& progress) noexcept
        -> progress_reporter_t;
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_REPORTER_HPP
