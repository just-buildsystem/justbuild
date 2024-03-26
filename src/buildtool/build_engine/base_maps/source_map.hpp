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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_SOURCE_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_SOURCE_MAP_HPP

#include <cstddef>
#include <unordered_set>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/base_maps/directory_map.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

namespace BuildMaps::Base {

using SourceTargetMap = AsyncMapConsumer<EntityName, AnalysedTargetPtr>;

auto CreateSourceTargetMap(const gsl::not_null<DirectoryEntriesMap*>& dirs,
                           gsl::not_null<RepositoryConfig*> const& repo_config,
                           std::size_t jobs = 0) -> SourceTargetMap;

}  // namespace BuildMaps::Base

#endif
