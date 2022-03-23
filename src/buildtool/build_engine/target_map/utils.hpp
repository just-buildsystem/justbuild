#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_UTILS_HPP

#include <optional>
#include <unordered_map>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/field_reader.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"

namespace BuildMaps::Target::Utils {

constexpr HashGenerator::HashType kActionHash = HashGenerator::HashType::SHA256;

auto obtainTargetByName(const SubExprEvaluator&,
                        const ExpressionPtr&,
                        const Configuration&,
                        const Base::EntityName&,
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

auto tree_conflict(const ExpressionPtr& /* map */)
    -> std::optional<std::string>;

auto getTainted(std::set<std::string>* tainted,
                const Configuration& config,
                const ExpressionPtr& tainted_exp,
                const BuildMaps::Target::TargetMap::LoggerPtr& logger) -> bool;

auto createAction(const ActionDescription::outputs_t& output_files,
                  const ActionDescription::outputs_t& output_dirs,
                  std::vector<std::string> command,
                  const ExpressionPtr& env,
                  std::optional<std::string> may_fail,
                  bool no_cache,
                  const ExpressionPtr& inputs_exp) -> ActionDescription::Ptr;

}  // namespace BuildMaps::Target::Utils
#endif
