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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_BUILT_IN_RULES_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_BUILT_IN_RULES_HPP

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/main/analyse_context.hpp"

namespace BuildMaps::Target {
auto HandleBuiltin(const gsl::not_null<AnalyseContext*>& context,
                   const nlohmann::json& rule_type,
                   const nlohmann::json& desc,
                   const BuildMaps::Target::ConfiguredTarget& key,
                   const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
                   const BuildMaps::Target::TargetMap::SetterPtr& setter,
                   const BuildMaps::Target::TargetMap::LoggerPtr& logger,
                   const gsl::not_null<BuildMaps::Target::ResultTargetMap*>&
                       result_map) -> bool;
}  // namespace BuildMaps::Target
#endif
