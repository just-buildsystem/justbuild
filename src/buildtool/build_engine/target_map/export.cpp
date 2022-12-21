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

#include <unordered_set>

#include "src/buildtool/build_engine/base_maps/field_reader.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/target_map/target_cache.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"

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
    const std::optional<TargetCache::Key>& target_cache_key,
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
            BuildMaps::Target::ConfiguredTarget{target, effective_config}),
        {(*value)->GraphInformation().Node()},
        {},
        {}};

    std::unordered_set<std::string> vars_set{};
    vars_set.insert(vars.begin(), vars.end());
    auto analysis_result = std::make_shared<AnalysedTarget const>(
        TargetResult{(*value)->Artifacts(), provides, (*value)->RunFiles()},
        std::vector<ActionDescription::Ptr>{},
        std::vector<std::string>{},
        std::vector<Tree::Ptr>{},
        std::move(vars_set),
        std::set<std::string>{},
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
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*> result_map) {
    auto desc = BuildMaps::Base::FieldReader::CreatePtr(
        desc_json, key.target, "export target", logger);
    desc->ExpectFields(kExpectedFields);
    auto exported_target_name = desc->ReadExpression("target");
    if (not exported_target_name) {
        return;
    }
    auto exported_target = BuildMaps::Base::ParseEntityNameFromExpression(
        exported_target_name,
        key.target,
        [&logger, &exported_target_name](std::string const& parse_err) {
            (*logger)(fmt::format("Parsing target name {} failed with:\n{}",
                                  exported_target_name->ToString(),
                                  parse_err),
                      true);
        });
    if (not exported_target) {
        return;
    }
    auto flexible_vars = desc->ReadStringList("flexible_config");
    if (not flexible_vars) {
        return;
    }
    auto effective_config = key.config.Prune(*flexible_vars);

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

    auto target_cache_key =
        TargetCache::Key::Create(*exported_target, target_config);
    if (target_cache_key) {
        if (auto target_cache_value =
                TargetCache::Instance().Read(*target_cache_key)) {
            auto const& [entry, info] = *target_cache_value;
            if (auto result = entry.ToResult()) {
                auto deps_info = TargetGraphInformation{
                    std::make_shared<BuildMaps::Target::ConfiguredTarget>(
                        BuildMaps::Target::ConfiguredTarget{key.target,
                                                            effective_config}),
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
                    deps_info);

                analysis_result = result_map->Add(key.target,
                                                  effective_config,
                                                  std::move(analysis_result),
                                                  std::nullopt,
                                                  true);

                Logger::Log(LogLevel::Performance,
                            "Export target {} served from cache: {} -> {}",
                            key.target.ToString(),
                            target_cache_key->Id().ToString(),
                            info.ToString());

                (*setter)(std::move(analysis_result));
                Statistics::Instance().IncrementExportsCachedCounter();
                return;
            }
            (*logger)(fmt::format("Reading target entry for key {} failed",
                                  target_cache_key->Id().ToString()),
                      false);
        }
        else {
            Statistics::Instance().IncrementExportsUncachedCounter();
            Logger::Log(LogLevel::Performance,
                        "Export target {} registered for caching: {}",
                        key.target.ToString(),
                        target_cache_key->Id().ToString());
        }
    }
    else {
        Statistics::Instance().IncrementExportsNotEligibleCounter();
        Logger::Log(LogLevel::Performance,
                    "Export target {} is not eligible for target caching",
                    key.target.ToString());
    }

    (*subcaller)(
        {BuildMaps::Target::ConfiguredTarget{std::move(*exported_target),
                                             std::move(target_config)}},
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
