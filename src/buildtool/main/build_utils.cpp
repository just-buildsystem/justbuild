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

#include "src/buildtool/main/build_utils.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"
#endif  // BOOTSTRAP_BUILD_TOOL

auto ReadOutputArtifacts(AnalysedTargetPtr const& target)
    -> std::pair<std::map<std::string, ArtifactDescription>,
                 std::map<std::string, ArtifactDescription>> {
    std::map<std::string, ArtifactDescription> artifacts{};
    std::map<std::string, ArtifactDescription> runfiles{};
    for (auto const& [path, artifact] : target->Artifacts()->Map()) {
        artifacts.emplace(path, artifact->Artifact());
    }
    for (auto const& [path, artifact] : target->RunFiles()->Map()) {
        if (not artifacts.contains(path)) {
            runfiles.emplace(path, artifact->Artifact());
        }
    }
    return {artifacts, runfiles};
}

auto CollectNonKnownArtifacts(
    std::unordered_map<TargetCacheKey, AnalysedTargetPtr> const& cache_targets)
    -> std::vector<ArtifactDescription> {
    auto cache_artifacts = std::unordered_set<ArtifactDescription>{};
    for (auto const& [_, target] : cache_targets) {
        auto artifacts = target->ContainedNonKnownArtifacts();
        cache_artifacts.insert(std::make_move_iterator(artifacts.begin()),
                               std::make_move_iterator(artifacts.end()));
    }
    return {std::make_move_iterator(cache_artifacts.begin()),
            std::make_move_iterator(cache_artifacts.end())};
}

auto ToTargetCacheWriteStrategy(std::string const& strategy)
    -> std::optional<TargetCacheWriteStrategy> {
    if (strategy == "disable") {
        return TargetCacheWriteStrategy::Disable;
    }
    if (strategy == "sync") {
        return TargetCacheWriteStrategy::Sync;
    }
    if (strategy == "split") {
        return TargetCacheWriteStrategy::Split;
    }
    return std::nullopt;
}

#ifndef BOOTSTRAP_BUILD_TOOL
void WriteTargetCacheEntries(
    std::unordered_map<TargetCacheKey, AnalysedTargetPtr> const& cache_targets,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        extra_infos,
    std::size_t jobs,
    gsl::not_null<IExecutionApi*> const& local_api,
    gsl::not_null<IExecutionApi*> const& remote_api,
    TargetCacheWriteStrategy strategy,
    TargetCache<true> const& tc) {
    if (strategy == TargetCacheWriteStrategy::Disable) {
        return;
    }
    if (!cache_targets.empty()) {
        Logger::Log(LogLevel::Info,
                    "Backing up artifacts of {} export targets",
                    cache_targets.size());
    }
    auto downloader = [&local_api, &remote_api, &jobs, strategy](auto infos) {
        return remote_api->ParallelRetrieveToCas(
            infos,
            local_api,
            jobs,
            strategy == TargetCacheWriteStrategy::Split);
    };
    for (auto const& [key, target] : cache_targets) {
        if (auto entry = TargetCacheEntry::FromTarget(target, extra_infos)) {
            if (not tc.Store(key, *entry, downloader)) {
                Logger::Log(LogLevel::Warning,
                            "Failed writing target cache entry for {}",
                            key.Id().ToString());
            }
        }
        else {
            Logger::Log(LogLevel::Warning,
                        "Failed creating target cache entry for {}",
                        key.Id().ToString());
        }
    }
    Logger::Log(LogLevel::Debug,
                "Finished backing up artifacts of export targets");
}
#endif  // BOOTSTRAP_BUILD_TOOL
