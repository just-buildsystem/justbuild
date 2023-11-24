// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_ABSENT_TARGET_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_ABSENT_TARGET_MAP_HPP
#include "gsl/gsl"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Target {
using AbsentTargetMap = AsyncMapConsumer<ConfiguredTarget, AnalysedTargetPtr>;

auto CreateAbsentTargetMap(const gsl::not_null<ResultTargetMap*>&,
                           gsl::not_null<RepositoryConfig*> const& repo_config,
                           std::size_t jobs = 0) -> AbsentTargetMap;

}  // namespace BuildMaps::Target
#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_ABSENT_TARGET_MAP_HPP
