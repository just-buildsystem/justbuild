#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_TARGETS_FILE_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_TARGETS_FILE_MAP_HPP

#include <filesystem>
#include <string>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/json_file_map.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Base {

using TargetsFileMap = AsyncMapConsumer<ModuleName, nlohmann::json>;

constexpr auto CreateTargetsFileMap =
    CreateJsonFileMap<&RepositoryConfig::TargetRoot,
                      &RepositoryConfig::TargetFileName,
                      /*kMandatory=*/true>;

}  // namespace BuildMaps::Base

#endif
