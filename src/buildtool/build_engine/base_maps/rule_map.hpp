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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_RULE_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_RULE_MAP_HPP

#include <memory>
#include <string>

#include "gsl-lite/gsl-lite.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/expression_map.hpp"
#include "src/buildtool/build_engine/base_maps/json_file_map.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/build_engine/base_maps/user_rule.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Base {

using RuleFileMap = AsyncMapConsumer<ModuleName, nlohmann::json>;

constexpr auto CreateRuleFileMap =
    CreateJsonFileMap<&RepositoryConfig::RuleRoot,
                      &RepositoryConfig::RuleFileName,
                      /*kMandatory=*/true>;

using UserRuleMap = AsyncMapConsumer<EntityName, UserRulePtr>;

auto CreateRuleMap(gsl::not_null<RuleFileMap*> const& rule_file_map,
                   gsl::not_null<ExpressionFunctionMap*> const& expr_map,
                   std::size_t jobs = 0) -> UserRuleMap;

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_RULE_MAP_HPP
