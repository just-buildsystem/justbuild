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
