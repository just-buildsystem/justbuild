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
#include <unordered_set>
#include <utility>  // std::move

#include "nlohmann/json.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#include "src/utils/cpp/json.hpp"
#endif

#ifndef BOOTSTRAP_BUILD_TOOL

namespace {
void WithFlexibleVariables(
    const BuildMaps::Target::ConfiguredTarget& key,
    std::vector<std::string> flexible_vars,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    const BuildMaps::Target::AbsentTargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::AbsentTargetMap::SetterPtr& setter,
    const BuildMaps::Target::AbsentTargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*> result_map,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& exports_progress,
    BuildMaps::Target::ServeFailureLogReporter* serve_failure_reporter) {
    auto effective_config = key.config.Prune(flexible_vars);
    if (key.config != effective_config) {
        (*subcaller)(
            {BuildMaps::Target::ConfiguredTarget{.target = key.target,
                                                 .config = effective_config}},
            [setter](auto const& values) {
                AnalysedTargetPtr result = *(values[0]);
                (*setter)(std::move(result));
            },
            logger);
        return;
    }

    // TODO(asartori): avoid code duplication in export.cpp
    stats->IncrementExportsFoundCounter();
    auto target_name = key.target.GetNamedTarget();
    auto repo_key = repo_config->RepositoryKey(target_name.repository);
    if (!repo_key) {
        (*logger)(fmt::format("Failed to obtain repository key for repo \"{}\"",
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
        auto task = fmt::format("[{},{}]",
                                key.target.ToString(),
                                PruneJson(effective_config.ToJson()).dump());
        Logger::Log(
            LogLevel::Debug,
            "Querying serve endpoint for absent export target {} with key {}",
            task,
            key.target.ToString());
        exports_progress->TaskTracker().Start(task);
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
        switch (auto const& ind = res->index(); ind) {
            case 0: {
                if (serve_failure_reporter != nullptr) {
                    (*serve_failure_reporter)(key, std::get<0>(*res));
                }
                // target found but failed to analyse/build: log it as fatal
                (*logger)(
                    fmt::format("Failure to remotely analyse or build absent "
                                "target {}\nDetailed log available on the "
                                "remote-execution endpoint as blob {}",
                                key.target.ToString(),
                                std::get<0>(*res)),
                    /*fatal=*/true);
                return;
            }
            case 1:  // fallthrough
            case 2: {
                // Other errors, including INTERNAL: log it as fatal
                (*logger)(fmt::format(
                              "While querying serve endpoint for absent export "
                              "target {}:\n{}",
                              key.target.ToString(),
                              ind == 1 ? std::get<1>(*res) : std::get<2>(*res)),
                          /*fatal=*/true);
                return;
            }
            default: {
                // index == 3
                target_cache_value = std::get<3>(*res);
                exports_progress->TaskTracker().Stop(task);
                from_just_serve = true;
            }
        }
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
            std::unordered_set<std::string>{flexible_vars.begin(),
                                            flexible_vars.end()},
            std::set<std::string>{},
            entry.ToImplied(),
            deps_info);

        analysis_result = result_map->Add(
            key.target, effective_config, analysis_result, std::nullopt, true);

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
}

}  // namespace

#endif

auto BuildMaps::Target::CreateAbsentTargetVariablesMap(std::size_t jobs)
    -> AbsentTargetVariablesMap {
#ifdef BOOTSTRAP_BUILD_TOOL
    auto target_variables = [](auto /*ts*/,
                               auto /*setter*/,
                               auto /*logger*/,
                               auto /*subcaller*/,
                               auto /*key*/) {};
#else
    auto target_variables = [](auto /*ts*/,
                               auto setter,
                               auto logger,
                               auto /*subcaller*/,
                               auto key) {
        auto flexible_vars_opt = ServeApi::ServeTargetVariables(
            key.target_root_id, key.target_file, key.target);
        if (!flexible_vars_opt) {
            (*logger)(fmt::format("Failed to obtain flexible config variables "
                                  "for absent target {}",
                                  key.target),
                      /*fatal=*/true);
            return;
        }
        (*setter)(std::move(flexible_vars_opt.value()));
    };
#endif
    return AbsentTargetVariablesMap{target_variables, jobs};
}

auto BuildMaps::Target::CreateAbsentTargetMap(
    const gsl::not_null<ResultTargetMap*>& result_map,
    const gsl::not_null<AbsentTargetVariablesMap*>& absent_variables,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    gsl::not_null<Statistics*> const& stats,
    gsl::not_null<Progress*> const& exports_progress,
    std::size_t jobs,
    BuildMaps::Target::ServeFailureLogReporter* serve_failure_reporter)
    -> AbsentTargetMap {
#ifndef BOOTSTRAP_BUILD_TOOL
    auto target_reader = [result_map,
                          repo_config,
                          stats,
                          exports_progress,
                          absent_variables,
                          serve_failure_reporter](auto ts,
                                                  auto setter,
                                                  auto logger,
                                                  auto subcaller,
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
        std::filesystem::path module{key.target.ToModule().module};
        auto vars_request = AbsentTargetDescription{
            .target_root_id = *target_root_id,
            .target_file =
                (module / *(repo_config->TargetFileName(repo_name))).string(),
            .target = key.target.GetNamedTarget().name};
        absent_variables->ConsumeAfterKeysReady(
            ts,
            {vars_request},
            [key,
             setter,
             repo_config,
             logger,
             serve_failure_reporter,
             result_map,
             stats,
             exports_progress,
             subcaller](auto const& values) {
                WithFlexibleVariables(key,
                                      *(values[0]),
                                      repo_config,
                                      subcaller,
                                      setter,
                                      logger,
                                      result_map,
                                      stats,
                                      exports_progress,
                                      serve_failure_reporter);
            },
            [logger, target = key.target](auto const& msg, auto fatal) {
                (*logger)(fmt::format("While requested the flexible "
                                      "variables of {}:\n{}",
                                      target.ToString(),
                                      msg),
                          fatal);
            });
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
