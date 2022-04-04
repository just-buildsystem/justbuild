#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"

#include <mutex>

#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/logging/logger.hpp"

auto BaseProgressReporter::Reporter() -> GraphTraverser::progress_reporter_t {
    return [](std::atomic<bool>* done, std::condition_variable* cv) {
        std::mutex m;
        auto const& stats = Statistics::Instance();
        std::unique_lock<std::mutex> lock(m);
        int64_t delay = kStartDelayMillis;
        while (not *done) {
            cv->wait_for(lock, std::chrono::milliseconds(delay));
            if (not *done) {
                // Note: order matters; queued has to be queried last
                int cached = stats.ActionsCachedCounter();
                int run = stats.ActionsExecutedCounter();
                int queued = stats.ActionsQueuedCounter();
                Logger::Log(LogLevel::Progress,
                            "{} cached, {} run, {} processing",
                            cached,
                            run,
                            queued - run - cached);
            }
            delay = delay * kDelayScalingFactorNumerator /
                    kDelayScalingFactorDenominator;
        }
    };
}

#endif
