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
