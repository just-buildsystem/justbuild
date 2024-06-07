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

#ifndef INCLUDED_SRC_BUILDOOL_MAIN_BUILD_UTILS_HPP
#define INCLUDED_SRC_BUILDOOL_MAIN_BUILD_UTILS_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/storage/target_cache.hpp"
#endif  // BOOTSTRAP_BUILD_TOOL

// Return disjoint maps for artifacts and runfiles
[[nodiscard]] auto ReadOutputArtifacts(AnalysedTargetPtr const& target)
    -> std::pair<std::map<std::string, ArtifactDescription>,
                 std::map<std::string, ArtifactDescription>>;

// Collect non-known artifacts
[[nodiscard]] auto CollectNonKnownArtifacts(
    std::unordered_map<TargetCacheKey, AnalysedTargetPtr> const& cache_targets)
    -> std::vector<ArtifactDescription>;

enum class TargetCacheWriteStrategy {
    Disable,  ///< Do not create target-level cache entries
    Sync,     ///< Create target-level cache entries after syncing the artifacts
    Split  ///< Create target-level cache entries after syncing the artifacts;
           ///< during artifact sync try to use blob splitting, if available
};

auto ToTargetCacheWriteStrategy(std::string const&)
    -> std::optional<TargetCacheWriteStrategy>;

#ifndef BOOTSTRAP_BUILD_TOOL
/// \brief Maps the Id of a TargetCacheKey to nullptr_t, as we only care if
/// writing the tc entry succeeds or not.
using TargetCacheWriterMap =
    AsyncMapConsumer<Artifact::ObjectInfo, std::nullptr_t>;

/// \brief Handles the writing of target cache keys after analysis concludes.
[[nodiscard]] auto CreateTargetCacheWriterMap(
    std::unordered_map<TargetCacheKey, AnalysedTargetPtr> const& cache_targets,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        extra_infos,
    std::size_t jobs,
    gsl::not_null<ApiBundle const*> const& apis,
    TargetCacheWriteStrategy strategy,
    TargetCache<true> const& tc) -> TargetCacheWriterMap;

// use explicit cast to std::function to allow template deduction when used
static const std::function<std::string(Artifact::ObjectInfo const&)>
    kObjectInfoPrinter = [](Artifact::ObjectInfo const& x) -> std::string {
    return x.ToString();
};

/// \brief Write the target cache entries resulting after a build.
/// \param strict_logging Bump warnings to actual errors.
void WriteTargetCacheEntries(
    std::unordered_map<TargetCacheKey, AnalysedTargetPtr> const& cache_targets,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        extra_infos,
    std::size_t jobs,
    ApiBundle const& apis,
    TargetCacheWriteStrategy strategy,
    TargetCache<true> const& tc,
    Logger const* logger = nullptr,
    LogLevel log_level = LogLevel::Warning);
#endif  // BOOTSTRAP_BUILD_TOOL

#endif  // INCLUDED_SRC_BUILDOOL_MAIN_BUILD_UTILS_HPP
