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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_EXPORTS_PROGRESS_REPORTER_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_EXPORTS_PROGRESS_REPORTER_HPP

#include "gsl/gsl"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"

/// \brief Reporter for progress in analysing export targets
class ExportsProgressReporter {
  public:
    [[nodiscard]] static auto Reporter(gsl::not_null<Statistics*> const& stats,
                                       gsl::not_null<Progress*> const& progress,
                                       bool has_serve) noexcept
        -> progress_reporter_t;
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_EXPORTS_PROGRESS_REPORTER_HPP
