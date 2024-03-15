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
#include "src/buildtool/build_engine/target_map/absent_target_map.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#endif

auto BuildMaps::Target::CreateAbsentTargetMap(
    const gsl::not_null<ResultTargetMap*>& result_map,
    gsl::not_null<RepositoryConfig*> const& repo_config,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& exports_progress,
    std::size_t jobs) -> AbsentTargetMap {
#ifndef BOOTSTRAP_BUILD_TOOL
    auto target_reader = [result_map, repo_config, stats, exports_progress](
                             auto /*ts*/,
                             auto setter,
                             auto logger,
                             auto /*subcaller*/,
                             auto key) {
        // assumptions:
        // - target with absent targets file requested
        // - ServeApi correctly configured
        auto const& repo_name = key.target.ToModule().repository;
        auto target_root_id =
            repo_config->TargetRoot(repo_name)->GetAbsentTreeId();
        if (!target_root_id) {
            (*logger)(fmt::format("Failed to get the target root id for "
                                  "repository \"{}\"",
                                  repo_name),
                      /*fatal=*/true);
            return;
        }
        auto flexible_vars = ServeApi::ServeTargetVariables(
            *target_root_id,
            *(repo_config->TargetFileName(repo_name)),
            key.target.GetNamedTarget().name);
        if (!flexible_vars) {
            (*logger)(fmt::format("Failed to obtain flexible config variables "
                                  "for absent target {}",
                                  key.target.ToString()),
                      /*fatal=*/true);
            return;
        }
        // we know now that this target is an export target
        stats->IncrementExportsFoundCounter();
        // TODO(asartori): avoid code duplication in export.cpp
        auto effective_config = key.config.Prune(*flexible_vars);
        auto target_name = key.target.GetNamedTarget();
        auto repo_key = repo_config->RepositoryKey(target_name.repository);
        if (!repo_key) {
            (*logger)(
                fmt::format("Failed to obtain repository key for repo \"{}\"",
                            target_name.repository),
                /*fatal=*/true);
            return;
        }
        auto target_cache_key =
            TargetCacheKey::Create(*repo_key, target_name, effective_config);
        if (not target_cache_key) {
            (*logger)(fmt::format("Could not produce cache key for target {}",
                                  key.target.ToString()),
                      /*fatal=*/true);
            return;
        }
        std::optional<std::pair<TargetCacheEntry, Artifact::ObjectInfo>>
            target_cache_value{std::nullopt};
        target_cache_value =
            Storage::Instance().TargetCache().Read(*target_cache_key);
        bool from_just_serve = false;
        if (!target_cache_value) {
            Logger::Log(LogLevel::Debug,
                        "Querying serve endpoint for absent export target {}",
                        key.target.ToString());
            exports_progress->TaskTracker().Start(
                target_cache_key->Id().ToString());
            auto res = ServeApi::ServeTarget(*target_cache_key, *repo_key);
            // process response from serve endpoint
            if (not res) {
                // report target not found
                (*logger)(fmt::format("Absent target {} was not found on serve "
                                      "endpoint",
                                      key.target.ToString()),
                          /*fatal=*/true);
                return;
            }
            if (res->index() == 0) {
                (*logger)(
                    fmt::format("Failure to remotely analyse or build absent "
                                "target {}\nDetailed log available on the "
                                "remote-execution endpoint as blob {}",
                                key.target.ToString(),
                                std::get<0>(*res)),
                    /*fatal=*/true);
                return;
            }
            if (res->index() == 1) {
                (*logger)(fmt::format("While querying serve endpoint for "
                                      "absent export target {}:\n{}",
                                      key.target.ToString(),
                                      std::get<1>(*res)),
                          /*fatal=*/true);
                return;
            }
            // index == 2
            target_cache_value = std::get<2>(*res);
            exports_progress->TaskTracker().Stop(
                target_cache_key->Id().ToString());
            from_just_serve = true;
        }

        if (!target_cache_value) {
            (*logger)(fmt::format("Could not get target cache value for key {}",
                                  target_cache_key->Id().ToString()),
                      /*fatal=*/true);
            return;
        }
        auto const& [entry, info] = *target_cache_value;
        if (auto result = entry.ToResult()) {
            auto deps_info = TargetGraphInformation{
                std::make_shared<BuildMaps::Target::ConfiguredTarget>(
                    BuildMaps::Target::ConfiguredTarget{
                        .target = key.target, .config = effective_config}),
                {},
                {},
                {}};

            auto analysis_result = std::make_shared<AnalysedTarget const>(
                *result,
                std::vector<ActionDescription::Ptr>{},
                std::vector<std::string>{},
                std::vector<Tree::Ptr>{},
                std::unordered_set<std::string>{flexible_vars->begin(),
                                                flexible_vars->end()},
                std::set<std::string>{},
                entry.ToImplied(),
                deps_info);

            analysis_result = result_map->Add(key.target,
                                              effective_config,
                                              analysis_result,
                                              std::nullopt,
                                              true);

            Logger::Log(LogLevel::Performance,
                        "Absent export target {} taken from {}: {} -> {}",
                        key.target.ToString(),
                        (from_just_serve ? "serve endpoint" : "cache"),
                        target_cache_key->Id().ToString(),
                        info.ToString());

            (*setter)(std::move(analysis_result));
            if (from_just_serve) {
                stats->IncrementExportsServedCounter();
            }
            else {
                stats->IncrementExportsCachedCounter();
            }
            return;
        }
        (*logger)(fmt::format("Reading target entry for key {} failed",
                              target_cache_key->Id().ToString()),
                  /*fatal=*/true);
    };
#else
    auto target_reader = [](auto /*ts*/,
                            auto /*setter*/,
                            auto /*logger*/,
                            auto /*subcaller*/,
                            auto /*key*/) {};
#endif
    return AbsentTargetMap{target_reader, jobs};
}
