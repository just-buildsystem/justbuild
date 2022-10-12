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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BASE_REPORTER_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BASE_REPORTER_HPP

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/graph_traverser/graph_traverser.hpp"

class BaseProgressReporter {
  public:
    static auto Reporter() -> GraphTraverser::progress_reporter_t;

  private:
    constexpr static int64_t kStartDelayMillis = 3000;
    // Scaling is roughly sqrt(2)
    constexpr static int64_t kDelayScalingFactorNumerator = 99;
    constexpr static int64_t kDelayScalingFactorDenominator = 70;
};

#endif

#endif
