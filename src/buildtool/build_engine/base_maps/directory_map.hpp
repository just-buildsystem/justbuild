#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_DIRECTORY_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_DIRECTORY_MAP_HPP

#include <filesystem>
#include <map>
#include <unordered_set>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Base {

using DirectoryEntriesMap =
    AsyncMapConsumer<ModuleName, FileRoot::DirectoryEntries>;

auto CreateDirectoryEntriesMap(std::size_t jobs = 0) -> DirectoryEntriesMap;

}  // namespace BuildMaps::Base

#endif
