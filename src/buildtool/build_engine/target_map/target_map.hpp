#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_TARGET_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_TARGET_MAP_HPP

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/base_maps/source_map.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Target {

using TargetMap = AsyncMapConsumer<ConfiguredTarget, AnalysedTargetPtr>;

auto CreateTargetMap(
    const gsl::not_null<BuildMaps::Base::SourceTargetMap*>&,
    const gsl::not_null<BuildMaps::Base::TargetsFileMap*>&,
    const gsl::not_null<BuildMaps::Base::UserRuleMap*>&,
    const gsl::not_null<BuildMaps::Base::DirectoryEntriesMap*>&,
    const gsl::not_null<ResultTargetMap*>&,
    std::size_t jobs = 0) -> TargetMap;

auto IsBuiltInRule(nlohmann::json const& rule_type) -> bool;

}  // namespace BuildMaps::Target

#endif
