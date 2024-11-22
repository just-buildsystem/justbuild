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

#ifndef INCLUDED_SRC_BUILDOOL_COMPUTED_ROOTS_ANALYSE_AND_BUILD_HPP
#define INCLUDED_SRC_BUILDOOL_COMPUTED_ROOTS_ANALYSE_AND_BUILD_HPP

#include <cstddef>
#include <optional>

#include "gsl/gsl"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/analyse.hpp"
#include "src/buildtool/main/analyse_context.hpp"

struct AnalyseAndBuildResult {
    AnalysisResult analysis_result;
    GraphTraverser::BuildResult build_result;
};

[[nodiscard]] auto AnalyseAndBuild(
    gsl::not_null<AnalyseContext*> const& analyse_context,
    GraphTraverser const& traverser,
    BuildMaps::Target::ConfiguredTarget const& id,
    std::size_t jobs,
    gsl::not_null<ApiBundle const*> const& apis,
    Logger const* logger = nullptr) -> std::optional<AnalyseAndBuildResult>;

#endif  // INCLUDED_SRC_BUILDOOL_COMPUTED_ROOTS_ANALYSE_AND_BUILD_HPP
