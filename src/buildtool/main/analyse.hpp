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

#ifndef INCLUDED_SRC_BUILDOOL_MAIN_ANALYSE_HPP
#define INCLUDED_SRC_BUILDOOL_MAIN_ANALYSE_HPP

#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/target_cache.hpp"

struct AnalysisResult {
    BuildMaps::Target::ConfiguredTarget id;
    AnalysedTargetPtr target;
    std::optional<std::string> modified;
};

[[nodiscard]] auto AnalyseTarget(
    const BuildMaps::Target::ConfiguredTarget& id,
    gsl::not_null<BuildMaps::Target::ResultTargetMap*> const& result_map,
    gsl::not_null<RepositoryConfig*> const& repo_config,
    ActiveTargetCache const& target_cache,
    gsl::not_null<Statistics*> const& stats,
    std::size_t jobs,
    std::optional<std::string> const& request_action_input,
    Logger const* logger = nullptr) -> std::optional<AnalysisResult>;
#endif
