#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_BUILT_IN_RULES_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_BUILT_IN_RULES_HPP

#include "gsl-lite/gsl-lite.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"

namespace BuildMaps::Target {
auto HandleBuiltin(
    const nlohmann::json& rule_type,
    const nlohmann::json& desc,
    const BuildMaps::Target::ConfiguredTarget& key,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map)
    -> bool;
}  // namespace BuildMaps::Target
#endif
