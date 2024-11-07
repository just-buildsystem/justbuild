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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_EXPRESSION_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_EXPRESSION_MAP_HPP

#include <cstddef>
#include <functional>
#include <string>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/expression_function.hpp"
#include "src/buildtool/build_engine/base_maps/json_file_map.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Base {

using ExpressionFileMap = AsyncMapConsumer<ModuleName, nlohmann::json>;

[[nodiscard]] static inline auto CreateExpressionFileMap(
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs) -> JsonFileMap {
    return CreateJsonFileMap<&RepositoryConfig::ExpressionRoot,
                             &RepositoryConfig::ExpressionFileName,
                             /*kMandatory=*/true>(repo_config, jobs);
}

using ExpressionFunctionMap =
    AsyncMapConsumer<EntityName, ExpressionFunctionPtr>;

auto CreateExpressionMap(
    gsl::not_null<ExpressionFileMap*> const& expr_file_map,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs = 0) -> ExpressionFunctionMap;

// use explicit cast to std::function to allow template deduction when used
static const std::function<std::string(EntityName const&)> kEntityNamePrinter =
    [](EntityName const& x) -> std::string { return x.ToString(); };

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_EXPRESSION_MAP_HPP
