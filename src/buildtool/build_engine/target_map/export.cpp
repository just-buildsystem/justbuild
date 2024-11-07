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

#include "src/buildtool/build_engine/target_map/export.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>  // std::move
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/analysed_target/target_graph_information.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/field_reader.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/common/tree.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/progress_reporting/task_tracker.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#include "src/utils/cpp/json.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#endif  // BOOTSTRAP_BUILD_TOOL

namespace {
auto const kExpectedFields = std::unordered_set<std::string>{"config_doc",
                                                             "doc",
                                                             "fixed_config",
                                                             "flexible_config",
                                                             "target",
                                                             "type"};

void FinalizeExport(
    const std::vector<AnalysedTargetPtr const*>& exported,
    const BuildMaps::Base::EntityName& target,
    const std::vector<std::string>& vars,
    const Configuration& effective_config,
    const std::optional<TargetCacheKey>& target_cache_key,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    const auto* value = exported[0];
    if (not(*value)->Tainted().empty()) {
        (*logger)("Only untainted targets can be exported.", true);
        return;
    }
    auto provides = (*value)->Provides();
    if (not provides->IsCacheable()) {
        (*logger)(fmt::format("Only cacheable values can be exported; but "
                              "target provides {}",
                              provides->ToString()),
                  true);
        return;
    }
    auto deps_info = TargetGraphInformation{
        std::make_shared<BuildMaps::Target::ConfiguredTarget>(
            BuildMaps::Target::ConfiguredTarget{.target = target,
                                                .config = effective_config}),
        {(*value)->GraphInformation().Node()},
        {},
        {}};

    std::unordered_set<std::string> vars_set{};
    vars_set.insert(vars.begin(), vars.end());
    std::set<std::string> implied{};
    if (target_cache_key) {
        implied.insert((*value)->ImpliedExport().begin(),
                       (*value)->ImpliedExport().end());
        implied.insert(target_cache_key->Id().digest.hash());
    }

    auto analysis_result = std::make_shared<AnalysedTarget const>(
        TargetResult{.artifact_stage = (*value)->Artifacts(),
                     .provides = provides,
                     .runfiles = (*value)->RunFiles()},
        std::vector<ActionDescription::Ptr>{},
        std::vector<std::string>{},
        std::vector<Tree::Ptr>{},
        std::move(vars_set),
        std::set<std::string>{},
        std::move(implied),
        std::move(deps_info));
    analysis_result = result_map->Add(target,
                                      effective_config,
                                      std::move(analysis_result),
                                      target_cache_key,
                                      true);
    (*setter)(std::move(analysis_result));
}

}  // namespace

void ExportRule(
    const gsl::not_null<AnalyseContext*>& context,
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    auto desc = BuildMaps::Base::FieldReader::CreatePtr(
        desc_json, key.target, "export target", logger);
    auto flexible_vars = desc->ReadStringList("flexible_config");
    if (not flexible_vars) {
        return;
    }
    auto effective_config = key.config.Prune(*flexible_vars);
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
    context->statistics->IncrementExportsFoundCounter();
    auto const& target_name = key.target.GetNamedTarget();
    auto repo_key = context->repo_config->RepositoryKey(*context->storage,
                                                        target_name.repository);
    auto target_cache_key = repo_key
                                ? context->storage->TargetCache().ComputeKey(
                                      *repo_key, target_name, effective_config)
                                : std::nullopt;

    if (target_cache_key) {
        // first try to get value from local target cache
        auto target_cache_value =
            context->storage->TargetCache().Read(*target_cache_key);
        bool from_just_serve{false};

#ifndef BOOTSTRAP_BUILD_TOOL
        // if not found locally, try the serve endpoint
        if (not target_cache_value and context->serve != nullptr) {
            Logger::Log(LogLevel::Debug,
                        "Querying serve endpoint for export target {}",
                        key.target.ToString());
            auto task =
                fmt::format("[{},{}]",
                            key.target.ToString(),
                            PruneJson(effective_config.ToJson()).dump());
            context->progress->TaskTracker().Start(task);
            auto res =
                context->serve->ServeTarget(*target_cache_key, *repo_key);
            // process response from serve endpoint
            if (not res) {
                // target not found: log to performance, and continue
                Logger::Log(LogLevel::Performance,
                            "Export target {} not known to serve endpoint",
                            key.target.ToString());
            }
            else {
                switch (res->index()) {
                    case 0: {
                        // target found but failed to analyse/build: this should
                        // be a fatal error for the local build too
                        (*logger)(
                            fmt::format(
                                "Failure to remotely analyse or build target "
                                "{}\nDetailed log available on the "
                                "remote-execution endpoint as blob {}",
                                key.target.ToString(),
                                std::get<0>(*res)),
                            /*fatal=*/true);
                        return;
                    }
                    case 1: {
                        // internal failure on the serve endpoint, or failures
                        // on the client side: local build should not continue
                        (*logger)(fmt::format("While querying serve endpoint "
                                              "for export target {}:\n{}",
                                              key.target.ToString(),
                                              std::get<1>(*res)),
                                  /*fatal=*/true);
                        return;
                    }
                    case 2: {
                        // some other failure occurred on the serve endpoint;
                        // log to debug and continue locally
                        Logger::Log(LogLevel::Debug,
                                    "While querying serve endpoint for export "
                                    "target {}:\n{}",
                                    key.target.ToString(),
                                    std::get<2>(*res));
                    } break;
                    default: {
                        // index == 3
                        target_cache_value = std::get<3>(*res);
                        from_just_serve = true;
                    }
                }
            }
            context->progress->TaskTracker().Stop(task);
        }
#endif  // BOOTSTRAP_BUILD_TOOL

        if (not target_cache_value) {
            context->statistics->IncrementExportsUncachedCounter();
            Logger::Log(LogLevel::Performance,
                        "Export target {} registered for caching: {}",
                        key.target.ToString(),
                        target_cache_key->Id().ToString());
        }
        else {
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
                                                  std::move(analysis_result),
                                                  std::nullopt,
                                                  true);

                Logger::Log(LogLevel::Performance,
                            "Export target {} taken from {}: {} -> {}",
                            key.target.ToString(),
                            (from_just_serve ? "serve endpoint" : "cache"),
                            target_cache_key->Id().ToString(),
                            info.ToString());

                (*setter)(std::move(analysis_result));
                if (from_just_serve) {
                    context->statistics->IncrementExportsServedCounter();
                }
                else {
                    context->statistics->IncrementExportsCachedCounter();
                }
                return;
            }
            (*logger)(fmt::format("Reading target entry for key {} failed",
                                  target_cache_key->Id().ToString()),
                      false);
        }
    }
    else {
        context->statistics->IncrementExportsNotEligibleCounter();
        Logger::Log(LogLevel::Performance,
                    "Export target {} is not eligible for target caching",
                    key.target.ToString());
    }

    desc->ExpectFields(kExpectedFields);
    auto exported_target_name = desc->ReadExpression("target");
    if (not exported_target_name) {
        return;
    }
    auto exported_target = BuildMaps::Base::ParseEntityNameFromExpression(
        exported_target_name,
        key.target,
        context->repo_config,
        [&logger, &exported_target_name](std::string const& parse_err) {
            (*logger)(fmt::format("Parsing target name {} failed with:\n{}",
                                  exported_target_name->ToString(),
                                  parse_err),
                      true);
        });
    if (not exported_target) {
        return;
    }
    auto fixed_config =
        desc->ReadOptionalExpression("fixed_config", Expression::kEmptyMap);
    if (not fixed_config->IsMap()) {
        (*logger)(fmt::format("fixed_config has to be a map, but found {}",
                              fixed_config->ToString()),
                  true);
        return;
    }
    for (auto const& var : fixed_config->Map().Keys()) {
        if (effective_config.VariableFixed(var)) {
            (*logger)(
                fmt::format("Variable {} is both fixed and flexible.", var),
                true);
            return;
        }
    }
    auto target_config = effective_config.Update(fixed_config);

    (*subcaller)(
        {BuildMaps::Target::ConfiguredTarget{
            .target = std::move(*exported_target),
            .config = std::move(target_config)}},
        [setter,
         logger,
         vars = std::move(*flexible_vars),
         result_map,
         effective_config = std::move(effective_config),
         target_cache_key = std::move(target_cache_key),
         target = key.target](auto const& values) {
            FinalizeExport(values,
                           target,
                           vars,
                           effective_config,
                           target_cache_key,
                           logger,
                           setter,
                           result_map);
        },
        logger);
}
