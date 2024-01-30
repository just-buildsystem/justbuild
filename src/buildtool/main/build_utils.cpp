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
#include "src/buildtool/multithreading/async_map_utils.hpp"
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
auto CreateTargetCacheWriterMap(
    std::unordered_map<TargetCacheKey, AnalysedTargetPtr> const& cache_targets,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        extra_infos,
    std::size_t jobs,
    gsl::not_null<IExecutionApi*> const& local_api,
    gsl::not_null<IExecutionApi*> const& remote_api,
    TargetCacheWriteStrategy strategy,
    TargetCache<true> const& tc) -> TargetCacheWriterMap {
    auto write_tc_entry =
        [cache_targets, extra_infos, jobs, local_api, remote_api, strategy, tc](
            auto /*ts*/,
            auto setter,
            auto logger,
            auto subcaller,
            auto const& key) {
            // get the TaretCacheKey corresponding to this Id
            TargetCacheKey tc_key{key};
            // check if entry actually needs storing
            if (not cache_targets.contains(tc_key)) {
                // sanity check: if not in the map, then it must be in cache
                if (not tc.Read(tc_key)) {
                    (*logger)(
                        fmt::format("Target-cache key {} is neither stored "
                                    "nor marked for storing",
                                    key.ToString()),
                        /*fatal=*/true);
                    return;
                }
                // entry already in target-cache, so nothing to be done
                (*setter)(nullptr);
                return;
            }
            auto const& target = cache_targets.at(tc_key);
            auto entry = TargetCacheEntry::FromTarget(target, extra_infos);
            if (not entry) {
                (*logger)(
                    fmt::format("Failed creating target cache entry for key {}",
                                key.ToString()),
                    /*fatal=*/true);
                return;
            }
            // only store current entry once all implied targets are stored
            if (auto implied_targets = entry->ToImpliedIds(key.digest.hash())) {
                (*subcaller)(
                    *implied_targets,
                    [tc_key,
                     entry,
                     jobs,
                     local_api,
                     remote_api,
                     strategy,
                     tc,
                     setter,
                     logger]([[maybe_unused]] auto const& values) {
                        // create parallel artifacts downloader
                        auto downloader = [&local_api,
                                           &remote_api,
                                           &jobs,
                                           strategy](auto infos) {
                            return remote_api->ParallelRetrieveToCas(
                                infos,
                                local_api,
                                jobs,
                                strategy == TargetCacheWriteStrategy::Split);
                        };
                        if (not tc.Store(tc_key, *entry, downloader)) {
                            (*logger)(fmt::format("Failed writing target cache "
                                                  "entry for {}",
                                                  tc_key.Id().ToString()),
                                      /*fatal=*/true);
                            return;
                        }
                        // success!
                        (*setter)(nullptr);
                    },
                    logger);
            }
            else {
                (*logger)(fmt::format("Failed retrieving implied targets for "
                                      "key {}",
                                      key.ToString()),
                          /*fatal=*/true);
            }
        };
    return AsyncMapConsumer<Artifact::ObjectInfo, std::nullptr_t>(
        write_tc_entry, jobs);
}

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
    // set up writer map
    auto tc_writer_map = CreateTargetCacheWriterMap(
        cache_targets, extra_infos, jobs, local_api, remote_api, strategy, tc);
    std::vector<Artifact::ObjectInfo> cache_targets_ids;
    cache_targets_ids.reserve(cache_targets.size());
    for (auto const& [k, _] : cache_targets) {
        cache_targets_ids.emplace_back(k.Id());
    }
    // write the target cache keys
    bool failed{false};
    {
        TaskSystem ts{jobs};
        tc_writer_map.ConsumeAfterKeysReady(
            &ts,
            cache_targets_ids,
            []([[maybe_unused]] auto _) {},  // map doesn't set anything
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(LogLevel::Warning,
                            "While writing target cache entries:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    // check for failures and cycles
    if (failed) {
        return;
    }
    if (auto error = DetectAndReportCycle(
            "writing cache targets", tc_writer_map, kObjectInfoPrinter)) {
        Logger::Log(LogLevel::Warning, *error);
        return;
    }

    Logger::Log(LogLevel::Debug,
                "Finished backing up artifacts of export targets");
}
#endif  // BOOTSTRAP_BUILD_TOOL
