#include "src/buildtool/build_engine/target_map/export.hpp"

#include <unordered_set>

#include "src/buildtool/build_engine/base_maps/field_reader.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"

namespace {
auto expectedFields = std::unordered_set<std::string>{"config_doc",
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
    std::unordered_set<std::string> vars_set{};
    vars_set.insert(vars.begin(), vars.end());
    // TODO(aehlig): wrap all artifacts into "save to target-cache" special
    // action
    auto analysis_result = std::make_shared<AnalysedTarget>(
        TargetResult{(*value)->Artifacts(), provides, (*value)->RunFiles()},
        std::vector<ActionDescription>{},
        std::vector<std::string>{},
        std::vector<Tree>{},
        std::move(vars_set),
        std::set<std::string>{});
    analysis_result =
        result_map->Add(target, effective_config, std::move(analysis_result));
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
    desc->ExpectFields(expectedFields);
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

    // TODO(aehlig): if the respository is content-fixed, look up in target
    // cache with key consistig of repository-description, target, and effective
    // config.

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
        {BuildMaps::Target::ConfiguredTarget{std::move(*exported_target),
                                             std::move(target_config)}},
        [setter,
         logger,
         vars = std::move(*flexible_vars),
         result_map,
         effective_config = std::move(effective_config),
         target = key.target](auto const& values) {
            FinalizeExport(values,
                           target,
                           vars,
                           effective_config,
                           logger,
                           setter,
                           result_map);
        },
        logger);
}
