#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_EXPORT_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_EXPORT_HPP

#include "gsl-lite/gsl-lite.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"

void ExportRule(const nlohmann::json& desc_json,
                const BuildMaps::Target::ConfiguredTarget& key,
                const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
                const BuildMaps::Target::TargetMap::SetterPtr& setter,
                const BuildMaps::Target::TargetMap::LoggerPtr& logger,
                gsl::not_null<BuildMaps::Target::ResultTargetMap*> result_map);

#endif
