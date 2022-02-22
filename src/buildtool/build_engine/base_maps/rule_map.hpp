#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_RULE_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_RULE_MAP_HPP

#include <memory>
#include <string>

#include "nlohmann/json.hpp"
#include "gsl-lite/gsl-lite.hpp"
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
