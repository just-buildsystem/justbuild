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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_UTILS_HPP

#include <optional>
#include <unordered_map>
#include <variant>

#include "gsl/gsl"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/field_reader.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/common/repository_config.hpp"

namespace BuildMaps::Target::Utils {

auto obtainTargetByName(const SubExprEvaluator&,
                        const ExpressionPtr&,
                        const Configuration&,
                        const Base::EntityName&,
                        const gsl::not_null<const RepositoryConfig*>&,
                        std::unordered_map<BuildMaps::Target::ConfiguredTarget,
                                           AnalysedTargetPtr> const&)
    -> AnalysedTargetPtr;

auto obtainTarget(const SubExprEvaluator&,
                  const ExpressionPtr&,
                  const Configuration&,
                  std::unordered_map<BuildMaps::Target::ConfiguredTarget,
                                     AnalysedTargetPtr> const&)
    -> AnalysedTargetPtr;

auto keys_expr(const ExpressionPtr& map) -> ExpressionPtr;

auto artifacts_tree(const ExpressionPtr& map)
    -> std::variant<std::string, ExpressionPtr>;

auto tree_conflict(const ExpressionPtr& /* map */)
    -> std::optional<std::string>;

auto add_dir_for(const std::string& cwd,
                 ExpressionPtr stage,
                 gsl::not_null<std::vector<Tree::Ptr>*> trees) -> ExpressionPtr;

auto getTainted(std::set<std::string>* tainted,
                const Configuration& config,
                const ExpressionPtr& tainted_exp,
                const BuildMaps::Target::TargetMap::LoggerPtr& logger) -> bool;

auto createAction(const ActionDescription::outputs_t& output_files,
                  const ActionDescription::outputs_t& output_dirs,
                  std::vector<std::string> command,
                  std::string cwd,
                  const ExpressionPtr& env,
                  std::optional<std::string> may_fail,
                  bool no_cache,
                  double timeout_scale,
                  const ExpressionPtr& execution_properties_exp,
                  const ExpressionPtr& inputs_exp) -> ActionDescription::Ptr;

}  // namespace BuildMaps::Target::Utils
#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_UTILS_HPP
