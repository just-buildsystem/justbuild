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

#include <cstddef>
#include <string>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/utils/cpp/hash_combine.hpp"

namespace BuildMaps::Target {

struct AbsentTargetDescription {
    std::string target_root_id;
    std::string target_file;
    std::string target;

    [[nodiscard]] auto operator==(
        AbsentTargetDescription const& other) const noexcept -> bool {
        return target_root_id == other.target_root_id &&
               target_file == other.target_file && target == other.target;
    }
};

using AbsentTargetMap = AsyncMapConsumer<ConfiguredTarget, AnalysedTargetPtr>;
using AbsentTargetVariablesMap =
    AsyncMapConsumer<AbsentTargetDescription, std::vector<std::string>>;

auto CreateAbsentTargetVariablesMap(std::size_t jobs = 0)
    -> AbsentTargetVariablesMap;

auto CreateAbsentTargetMap(const gsl::not_null<ResultTargetMap*>&,
                           const gsl::not_null<AbsentTargetVariablesMap*>&,
                           gsl::not_null<RepositoryConfig*> const& repo_config,
                           gsl::not_null<Statistics*> const& stats,
                           gsl::not_null<Progress*> const& exports_progress,
                           std::size_t jobs = 0) -> AbsentTargetMap;

}  // namespace BuildMaps::Target

namespace std {
template <>
struct hash<BuildMaps::Target::AbsentTargetDescription> {
    [[nodiscard]] auto operator()(
        const BuildMaps::Target::AbsentTargetDescription& t) const noexcept
        -> std::size_t {
        size_t seed{};
        hash_combine<std::string>(&seed, t.target_root_id);
        hash_combine<std::string>(&seed, t.target_file);
        hash_combine<std::string>(&seed, t.target);
        return seed;
    }
};
}  // namespace std

#endif
