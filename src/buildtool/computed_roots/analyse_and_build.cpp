// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/computed_roots/analyse_and_build.hpp"

#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/main/build_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/storage.hpp"

[[nodiscard]] auto AnalyseAndBuild(
    gsl::not_null<AnalyseContext*> const& analyse_context,
    GraphTraverser const& traverser,
    BuildMaps::Target::ConfiguredTarget const& id,
    std::size_t jobs,
    gsl::not_null<ApiBundle const*> const& apis,
    Logger const* logger) -> std::optional<AnalyseAndBuildResult> {
    auto analysis_result = AnalyseTarget(analyse_context,
                                         id,
                                         jobs,
                                         /*request_action_input*/ std::nullopt,
                                         logger);

    if (not analysis_result) {
        if (logger != nullptr) {
            logger->Emit(LogLevel::Warning,
                         "Failed to analyse target {}",
                         id.ToString());
        }
        return std::nullopt;
    }
    if (logger != nullptr) {
        logger->Emit(LogLevel::Info, "Analysed target {}", id.ToString());
    }

    auto const [artifacts, runfiles] =
        ReadOutputArtifacts(analysis_result->target);

    auto [actions, blobs, trees] = analysis_result->result_map.ToResult(
        analyse_context->statistics, analyse_context->progress, logger);

    auto const cache_targets = analysis_result->result_map.CacheTargets();

    // Clean up analyse_result map, now that it is no longer needed
    {
        TaskSystem ts{jobs};
        analysis_result->result_map.Clear(&ts);
    }
    auto extra_artifacts = CollectNonKnownArtifacts(cache_targets);
    for (auto const& [name, desc] : runfiles) {
        extra_artifacts.emplace_back(desc);
    }

    auto build_result = traverser.BuildAndStage(artifacts,
                                                {},
                                                std::move(actions),
                                                std::move(blobs),
                                                std::move(trees),
                                                std::move(extra_artifacts));

    if (not build_result) {
        if (logger != nullptr) {
            logger->Emit(
                LogLevel::Warning, "Build for target {} failed", id.ToString());
        }
        return std::nullopt;
    }
    WriteTargetCacheEntries(cache_targets,
                            build_result->extra_infos,
                            jobs,
                            *apis,
                            TargetCacheWriteStrategy::Sync,
                            analyse_context->storage->TargetCache(),
                            logger);

    return AnalyseAndBuildResult{.analysis_result = *std::move(analysis_result),
                                 .build_result = *std::move(build_result)};
}
