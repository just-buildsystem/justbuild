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

#include "src/buildtool/build_engine/target_map/built_in_rules.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "src/buildtool/build_engine/base_maps/field_reader.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/target_map/export.hpp"
#include "src/buildtool/build_engine/target_map/utils.hpp"
#include "src/utils/cpp/path.hpp"
#include "src/utils/cpp/vector.hpp"

namespace {

auto const kGenericRuleFields =
    std::unordered_set<std::string>{"arguments_config",
                                    "cmds",
                                    "deps",
                                    "env",
                                    "execution properties",
                                    "sh -c",
                                    "tainted",
                                    "timeout scaling",
                                    "type",
                                    "out_dirs",
                                    "outs"};

auto const kBlobGenRuleFields =
    std::unordered_set<std::string>{"arguments_config",
                                    "data",
                                    "deps",
                                    "name",
                                    "tainted",
                                    "type"};
auto const kTreeRuleFields = std::unordered_set<std::string>{"arguments_config",
                                                             "data",
                                                             "deps",
                                                             "name",
                                                             "tainted",
                                                             "type"};

auto const kInstallRuleFields =
    std::unordered_set<std::string>{"arguments_config",
                                    "deps",
                                    "dirs",
                                    "files",
                                    "tainted",
                                    "type"};

auto const kConfigureRuleFields =
    std::unordered_set<std::string>{"arguments_config",
                                    "config",
                                    "tainted",
                                    "target",
                                    "type"};

void BlobGenRuleWithDeps(
    const std::vector<BuildMaps::Target::ConfiguredTarget>& transition_keys,
    const std::vector<AnalysedTargetPtr const*>& dependency_values,
    const BuildMaps::Base::FieldReader::Ptr& desc,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map,
    const ObjectType& blob_type) {
    // Associate keys and values
    std::unordered_map<BuildMaps::Target::ConfiguredTarget, AnalysedTargetPtr>
        deps_by_transition;
    deps_by_transition.reserve(transition_keys.size());
    for (size_t i = 0; i < transition_keys.size(); ++i) {
        deps_by_transition.emplace(transition_keys[i], *dependency_values[i]);
    }

    auto param_vars = desc->ReadStringList("arguments_config");
    if (not param_vars) {
        return;
    }
    auto param_config = key.config.Prune(*param_vars);
    auto vars_set = std::unordered_set<std::string>{};
    vars_set.insert(param_vars->begin(), param_vars->end());
    for (auto const& dep : dependency_values) {
        vars_set.insert((*dep)->Vars().begin(), (*dep)->Vars().end());
    }
    auto effective_conf = key.config.Prune(vars_set);

    std::vector<BuildMaps::Target::ConfiguredTargetPtr> all_deps{};
    all_deps.reserve(dependency_values.size());
    for (auto const& dep : dependency_values) {
        all_deps.emplace_back((*dep)->GraphInformation().Node());
    }
    auto deps_info = TargetGraphInformation{
        std::make_shared<BuildMaps::Target::ConfiguredTarget>(
            BuildMaps::Target::ConfiguredTarget{key.target, effective_conf}),
        all_deps,
        {},
        {}};

    auto string_fields_fcts =
        FunctionMap::MakePtr(FunctionMap::underlying_map_t{
            {"outs",
             [&deps_by_transition, &key, repo_config](
                 auto&& eval, auto const& expr, auto const& env) {
                 return BuildMaps::Target::Utils::keys_expr(
                     BuildMaps::Target::Utils::obtainTargetByName(
                         eval,
                         expr,
                         env,
                         key.target,
                         repo_config,
                         deps_by_transition)
                         ->Artifacts());
             }},
            {"runfiles",
             [&deps_by_transition, &key, repo_config](
                 auto&& eval, auto const& expr, auto const& env) {
                 return BuildMaps::Target::Utils::keys_expr(
                     BuildMaps::Target::Utils::obtainTargetByName(
                         eval,
                         expr,
                         env,
                         key.target,
                         repo_config,
                         deps_by_transition)
                         ->RunFiles());
             }}});

    auto tainted = std::set<std::string>{};
    auto got_tainted = BuildMaps::Target::Utils::getTainted(
        &tainted,
        param_config,
        desc->ReadOptionalExpression("tainted", Expression::kEmptyList),
        logger);
    if (not got_tainted) {
        return;
    }
    for (auto const& dep : dependency_values) {
        if (not std::includes(tainted.begin(),
                              tainted.end(),
                              (*dep)->Tainted().begin(),
                              (*dep)->Tainted().end())) {
            (*logger)(
                "Not tainted with all strings the dependencies are tainted "
                "with",
                true);
            return;
        }
    }

    auto name_exp = desc->ReadOptionalExpression(
        "name", ExpressionPtr{std::string{"out.txt"}});
    if (not name_exp) {
        return;
    }
    auto name_val = name_exp.Evaluate(
        param_config, string_fields_fcts, [logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating name:\n{}", msg), true);
        });
    if (not name_val) {
        return;
    }
    if (not name_val->IsString()) {
        (*logger)(fmt::format("name should evaluate to a string, but got {}",
                              name_val->ToString()),
                  true);
        return;
    }
    auto data_exp =
        desc->ReadOptionalExpression("data", ExpressionPtr{std::string{""}});
    if (not data_exp) {
        return;
    }
    auto data_val = data_exp.Evaluate(
        param_config, string_fields_fcts, [logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating data:\n{}", msg), true);
        });
    if (not data_val) {
        return;
    }
    if (not data_val->IsString()) {
        (*logger)(fmt::format("data should evaluate to a string, but got {}",
                              data_val->ToString()),
                  true);
        return;
    }

    // if symlink target, we only accept non-upwards
    if (IsSymlinkObject(blob_type) and
        not PathIsNonUpwards(data_val->String())) {
        (*logger)(fmt::format("data string {} does not constitute a "
                              "non-upwards symlink target path",
                              data_val->String()),
                  true);
        return;
    }

    auto stage = ExpressionPtr{Expression::map_t{
        name_val->String(),
        ExpressionPtr{ArtifactDescription{
            ArtifactDigest::Create<ObjectType::File>(data_val->String()),
            blob_type}}}};

    auto analysis_result = std::make_shared<AnalysedTarget const>(
        TargetResult{.artifact_stage = stage,
                     .provides = ExpressionPtr{Expression::map_t{}},
                     .runfiles = stage},
        std::vector<ActionDescription::Ptr>{},
        std::vector<std::string>{data_val->String()},
        std::vector<Tree::Ptr>{},
        std::move(vars_set),
        std::move(tainted),
        std::move(deps_info));
    analysis_result =
        result_map->Add(key.target, effective_conf, std::move(analysis_result));
    (*setter)(std::move(analysis_result));
}

void BlobGenRule(
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map,
    const ObjectType& blob_type) {
    auto desc = BuildMaps::Base::FieldReader::CreatePtr(
        desc_json,
        key.target,
        IsSymlinkObject(blob_type) ? "symlink target"
                                   : "file-generation target",
        logger);
    desc->ExpectFields(kBlobGenRuleFields);
    auto param_vars = desc->ReadStringList("arguments_config");
    if (not param_vars) {
        return;
    }
    auto param_config = key.config.Prune(*param_vars);

    // Collect dependencies: deps
    auto const& empty_list = Expression::kEmptyList;
    auto deps_exp = desc->ReadOptionalExpression("deps", empty_list);
    if (not deps_exp) {
        return;
    }
    auto deps_value =
        deps_exp.Evaluate(param_config, {}, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating deps:\n{}", msg), true);
        });
    if (not deps_value) {
        return;
    }
    if (not deps_value->IsList()) {
        (*logger)(fmt::format("Expected deps to evaluate to a list of targets, "
                              "but found {}",
                              deps_value->ToString()),
                  true);
        return;
    }
    std::vector<BuildMaps::Target::ConfiguredTarget> dependency_keys;
    std::vector<BuildMaps::Target::ConfiguredTarget> transition_keys;
    dependency_keys.reserve(deps_value->List().size());
    transition_keys.reserve(deps_value->List().size());
    auto empty_transition = Configuration{Expression::kEmptyMap};
    for (auto const& dep_name : deps_value->List()) {
        auto dep_target = BuildMaps::Base::ParseEntityNameFromExpression(
            dep_name,
            key.target,
            repo_config,
            [&logger, &dep_name](std::string const& parse_err) {
                (*logger)(fmt::format("Parsing dep entry {} failed with:\n{}",
                                      dep_name->ToString(),
                                      parse_err),
                          true);
            });
        if (not dep_target) {
            return;
        }
        dependency_keys.emplace_back(
            BuildMaps::Target::ConfiguredTarget{*dep_target, key.config});
        transition_keys.emplace_back(
            BuildMaps::Target::ConfiguredTarget{*dep_target, empty_transition});
    }
    (*subcaller)(
        dependency_keys,
        [transition_keys = std::move(transition_keys),
         desc,
         setter,
         logger,
         key,
         repo_config,
         result_map,
         blob_type](auto const& values) {
            BlobGenRuleWithDeps(transition_keys,
                                values,
                                desc,
                                key,
                                repo_config,
                                setter,
                                logger,
                                result_map,
                                blob_type);
        },
        logger);
}

void FileGenRule(
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    BlobGenRule(desc_json,
                key,
                repo_config,
                subcaller,
                setter,
                logger,
                result_map,
                ObjectType::File);
}

void SymlinkRule(
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    BlobGenRule(desc_json,
                key,
                repo_config,
                subcaller,
                setter,
                logger,
                result_map,
                ObjectType::Symlink);
}

void TreeRuleWithDeps(
    const std::vector<AnalysedTargetPtr const*>& dependency_values,
    const std::string& name,
    const BuildMaps::Target::ConfiguredTarget& key,
    const BuildMaps::Base::FieldReader::Ptr& desc,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    auto param_vars = desc->ReadStringList("arguments_config");
    if (not param_vars) {
        return;
    }
    auto param_config = key.config.Prune(*param_vars);
    auto tainted = std::set<std::string>{};
    auto got_tainted = BuildMaps::Target::Utils::getTainted(
        &tainted,
        param_config,
        desc->ReadOptionalExpression("tainted", Expression::kEmptyList),
        logger);
    if (not got_tainted) {
        return;
    }
    for (auto const& dep : dependency_values) {
        if (not std::includes(tainted.begin(),
                              tainted.end(),
                              (*dep)->Tainted().begin(),
                              (*dep)->Tainted().end())) {
            (*logger)(
                "Not tainted with all strings the dependencies are tainted "
                "with",
                true);
            return;
        }
    }

    auto vars_set = std::unordered_set<std::string>{};
    vars_set.insert(param_vars->begin(), param_vars->end());
    for (auto const& dep : dependency_values) {
        vars_set.insert((*dep)->Vars().begin(), (*dep)->Vars().end());
    }
    auto effective_conf = key.config.Prune(vars_set);
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> all_deps{};
    all_deps.reserve(dependency_values.size());
    for (auto const& dep : dependency_values) {
        all_deps.emplace_back((*dep)->GraphInformation().Node());
    }
    auto deps_info = TargetGraphInformation{
        std::make_shared<BuildMaps::Target::ConfiguredTarget>(
            BuildMaps::Target::ConfiguredTarget{key.target, effective_conf}),
        all_deps,
        {},
        {}};

    // Compute the stage
    auto stage = ExpressionPtr{Expression::map_t{}};
    for (auto const& dep : dependency_values) {
        auto to_stage = ExpressionPtr{
            Expression::map_t{(*dep)->RunFiles(), (*dep)->Artifacts()}};
        auto dup = stage->Map().FindConflictingDuplicate(to_stage->Map());
        if (dup) {
            (*logger)(fmt::format("Staging conflict for path {}", dup->get()),
                      true);
            return;
        }
        stage = ExpressionPtr{Expression::map_t{stage, to_stage}};
    }

    // Result is the associated tree, located at name
    auto conflict = BuildMaps::Target::Utils::tree_conflict(stage);
    if (conflict) {
        (*logger)(fmt::format("TREE conflict on subtree {}", *conflict), true);
        return;
    }
    std::unordered_map<std::string, ArtifactDescription> tree_content;
    tree_content.reserve(stage->Map().size());
    for (auto const& [input_path, artifact] : stage->Map()) {
        auto norm_path = ToNormalPath(std::filesystem::path{input_path});
        tree_content.emplace(std::move(norm_path), artifact->Artifact());
    }
    auto tree = std::make_shared<Tree>(std::move(tree_content));
    auto tree_id = tree->Id();
    std::vector<Tree::Ptr> trees{};
    trees.emplace_back(std::move(tree));
    auto result_stage = Expression::map_t::underlying_map_t{};
    result_stage.emplace(name, ArtifactDescription{tree_id});
    auto result = ExpressionPtr{Expression::map_t{result_stage}};

    auto analysis_result = std::make_shared<AnalysedTarget const>(
        TargetResult{.artifact_stage = result,
                     .provides = ExpressionPtr{Expression::map_t{}},
                     .runfiles = result},
        std::vector<ActionDescription::Ptr>{},
        std::vector<std::string>{},
        std::move(trees),
        std::move(vars_set),
        std::move(tainted),
        std::move(deps_info));
    analysis_result =
        result_map->Add(key.target, effective_conf, std::move(analysis_result));
    (*setter)(std::move(analysis_result));
}

void TreeRule(
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    auto desc = BuildMaps::Base::FieldReader::CreatePtr(
        desc_json, key.target, "tree target", logger);
    desc->ExpectFields(kTreeRuleFields);
    auto param_vars = desc->ReadStringList("arguments_config");
    if (not param_vars) {
        return;
    }
    auto param_config = key.config.Prune(*param_vars);

    // Collect dependencies: deps
    auto const& empty_list = Expression::kEmptyList;
    auto deps_exp = desc->ReadOptionalExpression("deps", empty_list);
    if (not deps_exp) {
        return;
    }
    auto deps_value =
        deps_exp.Evaluate(param_config, {}, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating deps:\n{}", msg), true);
        });
    if (not deps_value) {
        return;
    }
    if (not deps_value->IsList()) {
        (*logger)(fmt::format("Expected deps to evaluate to a list of targets, "
                              "but found {}",
                              deps_value->ToString()),
                  true);
        return;
    }
    std::vector<BuildMaps::Target::ConfiguredTarget> dependency_keys;
    for (auto const& dep_name : deps_value->List()) {
        auto dep_target = BuildMaps::Base::ParseEntityNameFromExpression(
            dep_name,
            key.target,
            repo_config,
            [&logger, &dep_name](std::string const& parse_err) {
                (*logger)(fmt::format("Parsing dep entry {} failed with:\n{}",
                                      dep_name->ToString(),
                                      parse_err),
                          true);
            });
        if (not dep_target) {
            return;
        }
        dependency_keys.emplace_back(
            BuildMaps::Target::ConfiguredTarget{*dep_target, key.config});
    }
    auto name_exp =
        desc->ReadOptionalExpression("name", ExpressionPtr{std::string{""}});
    if (not name_exp) {
        return;
    }
    auto name_value =
        name_exp.Evaluate(param_config, {}, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating name:\n{}", msg), true);
        });
    if (not name_value) {
        return;
    }
    if (not name_value->IsString()) {
        (*logger)(
            fmt::format("Expected name to evaluate to a string, but got {}",
                        name_value->ToString()),
            true);
        return;
    }
    (*subcaller)(
        dependency_keys,
        [name = name_value->String(), desc, setter, logger, key, result_map](
            auto const& values) {
            TreeRuleWithDeps(
                values, name, key, desc, setter, logger, result_map);
        },
        logger);
}

void InstallRuleWithDeps(
    const std::vector<BuildMaps::Target::ConfiguredTarget>& dependency_keys,
    const std::vector<AnalysedTargetPtr const*>& dependency_values,
    const BuildMaps::Base::FieldReader::Ptr& desc,
    const BuildMaps::Target::ConfiguredTarget& key,
    const std::vector<BuildMaps::Base::EntityName>& deps,
    const std::unordered_map<std::string, BuildMaps::Base::EntityName>& files,
    const std::vector<std::pair<BuildMaps::Base::EntityName, std::string>>&
        dirs,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    // Associate keys and values
    std::unordered_map<BuildMaps::Base::EntityName, AnalysedTargetPtr>
        deps_by_target;
    deps_by_target.reserve(dependency_keys.size());
    for (size_t i = 0; i < dependency_keys.size(); ++i) {
        deps_by_target.emplace(dependency_keys[i].target,
                               *dependency_values[i]);
    }

    // Compute the effective dependecy on config variables
    std::unordered_set<std::string> effective_vars;
    auto param_vars = desc->ReadStringList("arguments_config");
    effective_vars.insert(param_vars->begin(), param_vars->end());
    for (auto const& [target_name, target] : deps_by_target) {
        effective_vars.insert(target->Vars().begin(), target->Vars().end());
    }
    auto effective_conf = key.config.Prune(effective_vars);

    std::vector<BuildMaps::Target::ConfiguredTargetPtr> all_deps{};
    all_deps.reserve(dependency_values.size());
    for (auto const& dep : dependency_values) {
        all_deps.emplace_back((*dep)->GraphInformation().Node());
    }

    auto deps_info = TargetGraphInformation{
        std::make_shared<BuildMaps::Target::ConfiguredTarget>(
            BuildMaps::Target::ConfiguredTarget{key.target, effective_conf}),
        all_deps,
        {},
        {}};

    // Compute and verify taintedness
    auto tainted = std::set<std::string>{};
    auto got_tainted = BuildMaps::Target::Utils::getTainted(
        &tainted,
        key.config.Prune(*param_vars),
        desc->ReadOptionalExpression("tainted", Expression::kEmptyList),
        logger);
    if (not got_tainted) {
        return;
    }
    for (auto const& dep : dependency_values) {
        if (not std::includes(tainted.begin(),
                              tainted.end(),
                              (*dep)->Tainted().begin(),
                              (*dep)->Tainted().end())) {
            (*logger)(
                "Not tainted with all strings the dependencies are tainted "
                "with",
                true);
            return;
        }
    }

    // Stage deps (runfiles only)
    auto stage = ExpressionPtr{Expression::map_t{}};
    for (auto const& dep : deps) {
        auto to_stage = deps_by_target.at(dep)->RunFiles();
        auto dup = stage->Map().FindConflictingDuplicate(to_stage->Map());
        if (dup) {
            (*logger)(fmt::format("Staging conflict for path {}", dup->get()),
                      true);
            return;
        }
        stage = ExpressionPtr{Expression::map_t{stage, to_stage}};
    }

    // stage files (artifacts, but fall back to runfiles)
    auto files_stage = Expression::map_t::underlying_map_t{};
    for (auto const& [path, target] : files) {
        if (stage->Map().contains(path)) {
            (*logger)(fmt::format("Staging conflict for path {}", path), true);
            return;
        }
        auto artifacts = deps_by_target[target]->Artifacts();
        if (artifacts->Map().empty()) {
            // If no artifacts are present, fall back to runfiles
            artifacts = deps_by_target[target]->RunFiles();
        }
        if (artifacts->Map().empty()) {
            (*logger)(fmt::format(
                          "No artifacts or runfiles for {} to be staged to {}",
                          target.ToString(),
                          path),
                      true);
            return;
        }
        if (artifacts->Map().size() != 1) {
            (*logger)(
                fmt::format("Not precisely one entry for {} to be staged to {}",
                            target.ToString(),
                            path),
                true);
            return;
        }
        files_stage.emplace(path, artifacts->Map().Values()[0]);
    }
    stage = ExpressionPtr{Expression::map_t{stage, files_stage}};

    // stage dirs (artifacts and runfiles)
    for (auto const& subdir : dirs) {
        auto subdir_stage = Expression::map_t::underlying_map_t{};
        auto dir_path = std::filesystem::path{subdir.second};
        auto target = deps_by_target.at(subdir.first);
        // within a target, artifacts and runfiles may overlap, but artifacts
        // take perference
        for (auto const& [path, artifact] : target->Artifacts()->Map()) {
            subdir_stage.emplace(ToNormalPath(dir_path / path).string(),
                                 artifact);
        }
        for (auto const& [path, artifact] : target->RunFiles()->Map()) {
            subdir_stage.emplace(ToNormalPath(dir_path / path).string(),
                                 artifact);
        }
        auto to_stage = ExpressionPtr{Expression::map_t{subdir_stage}};
        auto dup = stage->Map().FindConflictingDuplicate(to_stage->Map());
        if (dup) {
            (*logger)(fmt::format("Staging conflict for path {}", dup->get()),
                      true);
            return;
        }
        stage = ExpressionPtr{Expression::map_t{stage, to_stage}};
    }

    auto conflict = BuildMaps::Target::Utils::tree_conflict(stage);
    if (conflict) {
        (*logger)(fmt::format("TREE conflict on subtree {}", *conflict), true);
        return;
    }
    auto const& empty_map = Expression::kEmptyMap;
    auto result = std::make_shared<AnalysedTarget const>(
        TargetResult{
            .artifact_stage = stage, .provides = empty_map, .runfiles = stage},
        std::vector<ActionDescription::Ptr>{},
        std::vector<std::string>{},
        std::vector<Tree::Ptr>{},
        std::move(effective_vars),
        std::move(tainted),
        std::move(deps_info));

    result = result_map->Add(key.target, effective_conf, std::move(result));
    (*setter)(std::move(result));
}

void InstallRule(
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    auto desc = BuildMaps::Base::FieldReader::CreatePtr(
        desc_json, key.target, "install target", logger);
    desc->ExpectFields(kInstallRuleFields);
    auto param_vars = desc->ReadStringList("arguments_config");
    if (not param_vars) {
        return;
    }
    auto param_config = key.config.Prune(*param_vars);

    // Collect dependencies: deps
    auto const& empty_list = Expression::kEmptyList;
    auto deps_exp = desc->ReadOptionalExpression("deps", empty_list);
    if (not deps_exp) {
        return;
    }
    auto deps_value =
        deps_exp.Evaluate(param_config, {}, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating deps:\n{}", msg), true);
        });
    if (not deps_value) {
        return;
    }
    if (not deps_value->IsList()) {
        (*logger)(fmt::format("Expected deps to evaluate to a list of targets, "
                              "but found {}",
                              deps_value->ToString()),
                  true);
        return;
    }
    std::vector<BuildMaps::Target::ConfiguredTarget> dependency_keys;
    std::vector<BuildMaps::Base::EntityName> deps;
    deps.reserve(deps_value->List().size());
    for (auto const& dep_name : deps_value->List()) {
        auto dep_target = BuildMaps::Base::ParseEntityNameFromExpression(
            dep_name,
            key.target,
            repo_config,
            [&logger, &dep_name](std::string const& parse_err) {
                (*logger)(fmt::format("Parsing dep entry {} failed with:\n{}",
                                      dep_name->ToString(),
                                      parse_err),
                          true);
            });
        if (not dep_target) {
            return;
        }
        dependency_keys.emplace_back(
            BuildMaps::Target::ConfiguredTarget{*dep_target, key.config});
        deps.emplace_back(*dep_target);
    }

    // Collect dependencies: files
    auto const& empty_map = Expression::kEmptyMap;
    auto files_exp = desc->ReadOptionalExpression("files", empty_map);
    if (not files_exp) {
        return;
    }
    if (not files_exp->IsMap()) {
        (*logger)(fmt::format("Expected files to be a map of target "
                              "expressions, but found {}",
                              files_exp->ToString()),
                  true);
        return;
    }
    auto files = std::unordered_map<std::string, BuildMaps::Base::EntityName>{};
    files.reserve(files_exp->Map().size());
    for (auto const& [path, dep_exp] : files_exp->Map()) {
        std::string path_ = path;  // Have a variable to capture
        auto dep_name = dep_exp.Evaluate(
            param_config, {}, [&logger, &path_](auto const& msg) {
                (*logger)(
                    fmt::format(
                        "While evaluating files entry for {}:\n{}", path_, msg),
                    true);
            });
        if (not dep_name) {
            return;
        }
        auto dep_target = BuildMaps::Base::ParseEntityNameFromExpression(
            dep_name,
            key.target,
            repo_config,
            [&logger, &dep_name, &path = path](std::string const& parse_err) {
                (*logger)(fmt::format("Parsing file entry {} for key {} failed "
                                      "with:\n{}",
                                      dep_name->ToString(),
                                      path,
                                      parse_err),
                          true);
            });
        if (not dep_target) {
            return;
        }
        dependency_keys.emplace_back(
            BuildMaps::Target::ConfiguredTarget{*dep_target, key.config});
        files.emplace(path, *dep_target);
    }

    // Collect dependencies: dirs
    auto dirs_exp = desc->ReadOptionalExpression("dirs", empty_list);
    if (not dirs_exp) {
        return;
    }
    auto dirs_value =
        dirs_exp.Evaluate(param_config, {}, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating deps:\n{}", msg), true);
        });
    if (not dirs_value) {
        return;
    }
    if (not dirs_value->IsList()) {
        (*logger)(fmt::format("Expected dirs to evaluate to a list of "
                              "path-target pairs, but found {}",
                              dirs_value->ToString()),
                  true);
        return;
    }
    auto dirs =
        std::vector<std::pair<BuildMaps::Base::EntityName, std::string>>{};
    dirs.reserve(dirs_value->List().size());
    for (auto const& entry : dirs_value->List()) {
        if (not(entry->IsList() and entry->List().size() == 2 and
                entry->List()[1]->IsString())) {
            (*logger)(fmt::format("Expected dirs to evaluate to a list of "
                                  "target-path pairs, but found entry {}",
                                  entry->ToString()),
                      true);
            return;
        }
        auto dep_target = BuildMaps::Base::ParseEntityNameFromExpression(
            entry->List()[0],
            key.target,
            repo_config,
            [&logger, &entry](std::string const& parse_err) {
                (*logger)(fmt::format("Parsing dir entry {} for path {} failed "
                                      "with:\n{}",
                                      entry->List()[0]->ToString(),
                                      entry->List()[1]->String(),
                                      parse_err),
                          true);
            });
        if (not dep_target) {
            return;
        }
        dependency_keys.emplace_back(
            BuildMaps::Target::ConfiguredTarget{*dep_target, key.config});
        dirs.emplace_back(std::pair<BuildMaps::Base::EntityName, std::string>{
            *dep_target, entry->List()[1]->String()});
    }

    (*subcaller)(
        dependency_keys,
        [dependency_keys,
         deps = std::move(deps),
         files = std::move(files),
         dirs = std::move(dirs),
         desc,
         setter,
         logger,
         key,
         result_map](auto const& values) {
            InstallRuleWithDeps(dependency_keys,
                                values,
                                desc,
                                key,
                                deps,
                                files,
                                dirs,
                                setter,
                                logger,
                                result_map);
        },
        logger);
}

void GenericRuleWithDeps(
    const std::vector<BuildMaps::Target::ConfiguredTarget>& transition_keys,
    const std::vector<AnalysedTargetPtr const*>& dependency_values,
    const BuildMaps::Base::FieldReader::Ptr& desc,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    // Associate dependency keys with values
    std::unordered_map<BuildMaps::Target::ConfiguredTarget, AnalysedTargetPtr>
        deps_by_transition;
    deps_by_transition.reserve(transition_keys.size());
    for (size_t i = 0; i < transition_keys.size(); ++i) {
        deps_by_transition.emplace(transition_keys[i], *dependency_values[i]);
    }

    // Compute the effective dependency on config variables
    std::unordered_set<std::string> effective_vars;
    auto param_vars = desc->ReadStringList("arguments_config");
    effective_vars.insert(param_vars->begin(), param_vars->end());
    for (auto const& [transition, target] : deps_by_transition) {
        effective_vars.insert(target->Vars().begin(), target->Vars().end());
    }
    auto effective_conf = key.config.Prune(effective_vars);

    std::vector<BuildMaps::Target::ConfiguredTargetPtr> all_deps{};
    all_deps.reserve(dependency_values.size());
    for (auto const& dep : dependency_values) {
        all_deps.emplace_back((*dep)->GraphInformation().Node());
    }

    auto deps_info = TargetGraphInformation{
        std::make_shared<BuildMaps::Target::ConfiguredTarget>(
            BuildMaps::Target::ConfiguredTarget{key.target, effective_conf}),
        all_deps,
        {},
        {}};

    // Compute and verify taintedness
    auto tainted = std::set<std::string>{};
    auto got_tainted = BuildMaps::Target::Utils::getTainted(
        &tainted,
        key.config.Prune(*param_vars),
        desc->ReadOptionalExpression("tainted", Expression::kEmptyList),
        logger);
    if (not got_tainted) {
        return;
    }
    for (auto const& dep : dependency_values) {
        if (not std::includes(tainted.begin(),
                              tainted.end(),
                              (*dep)->Tainted().begin(),
                              (*dep)->Tainted().end())) {
            (*logger)(
                "Not tainted with all strings the dependencies are tainted "
                "with",
                true);
            return;
        }
    }

    // Evaluate cmd, outs, env
    auto string_fields_fcts =
        FunctionMap::MakePtr(FunctionMap::underlying_map_t{
            {"outs",
             [&deps_by_transition, &key, repo_config](
                 auto&& eval, auto const& expr, auto const& env) {
                 return BuildMaps::Target::Utils::keys_expr(
                     BuildMaps::Target::Utils::obtainTargetByName(
                         eval,
                         expr,
                         env,
                         key.target,
                         repo_config,
                         deps_by_transition)
                         ->Artifacts());
             }},
            {"runfiles",
             [&deps_by_transition, &key, repo_config](
                 auto&& eval, auto const& expr, auto const& env) {
                 return BuildMaps::Target::Utils::keys_expr(
                     BuildMaps::Target::Utils::obtainTargetByName(
                         eval,
                         expr,
                         env,
                         key.target,
                         repo_config,
                         deps_by_transition)
                         ->RunFiles());
             }}});
    auto const& empty_list = Expression::kEmptyList;
    auto param_config = key.config.Prune(*param_vars);

    auto outs_exp = desc->ReadOptionalExpression("outs", empty_list);
    auto out_dirs_exp = desc->ReadOptionalExpression("out_dirs", empty_list);

    std::vector<std::string> outs{};
    std::vector<std::string> out_dirs{};
    if (outs_exp) {
        auto outs_value = outs_exp.Evaluate(
            param_config, string_fields_fcts, [&logger](auto const& msg) {
                (*logger)(fmt::format("While evaluating outs:\n{}", msg), true);
            });
        if (not outs_value) {
            return;
        }
        if (not outs_value->IsList()) {
            (*logger)(fmt::format("outs has to evaluate to a list of "
                                  "strings, but found {}",
                                  outs_value->ToString()),
                      true);
            return;
        }
        if (not outs_value->List().empty()) {
            outs.reserve(outs_value->List().size());
            for (auto const& x : outs_value->List()) {
                if (not x->IsString()) {
                    (*logger)(fmt::format("outs has to evaluate to a list of "
                                          "strings, but found entry {}",
                                          x->ToString()),
                              true);
                    return;
                }
                outs.emplace_back(x->String());
            }
        }
    }
    if (out_dirs_exp) {
        auto out_dirs_value = out_dirs_exp.Evaluate(
            param_config, string_fields_fcts, [&logger](auto const& msg) {
                (*logger)(fmt::format("While evaluating out_dirs:\n{}", msg),
                          true);
            });
        if (not out_dirs_value) {
            return;
        }
        if (not out_dirs_value->IsList()) {
            (*logger)(fmt::format("out_dirs has to evaluate to a list of "
                                  "strings, but found {}",
                                  out_dirs_value->ToString()),
                      true);
            return;
        }
        if (not out_dirs_value->List().empty()) {
            out_dirs.reserve(out_dirs_value->List().size());
            for (auto const& x : out_dirs_value->List()) {
                if (not x->IsString()) {
                    (*logger)(
                        fmt::format("out_dirs has to evaluate to a list of "
                                    "strings, but found entry {}",
                                    x->ToString()),
                        true);
                    return;
                }
                out_dirs.emplace_back(x->String());
            }
        }
    }

    if (outs.empty() and out_dirs.empty()) {
        (*logger)(
            R"(At least one of "outs" and "out_dirs" must be specified for "generic")",
            true);
        return;
    }

    sort_and_deduplicate(&outs);
    sort_and_deduplicate(&out_dirs);

    // looking for same paths in both outs and out_dirs
    std::vector<std::string> intersection;
    std::set_intersection(outs.begin(),
                          outs.end(),
                          out_dirs.begin(),
                          out_dirs.end(),
                          std::back_inserter(intersection));
    if (not intersection.empty()) {
        (*logger)(fmt::format("outs and out_dirs for generic must be disjoint. "
                              "Found repeated entries:\n{}",
                              nlohmann::json(intersection).dump()),
                  true);
        return;
    }

    auto cmd_exp = desc->ReadOptionalExpression("cmds", empty_list);
    if (not cmd_exp) {
        return;
    }
    auto cmd_value = cmd_exp.Evaluate(
        param_config, string_fields_fcts, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating cmds:\n{}", msg), true);
        });
    if (not cmd_value) {
        return;
    }
    if (not cmd_value->IsList()) {
        (*logger)(fmt::format(
                      "cmds has to evaluate to a list of strings, but found {}",
                      cmd_value->ToString()),
                  true);
        return;
    }
    std::stringstream cmd_ss{};
    for (auto const& x : cmd_value->List()) {
        if (not x->IsString()) {
            (*logger)(fmt::format("cmds has to evaluate to a list of strings, "
                                  "but found entry {}",
                                  x->ToString()),
                      true);
            return;
        }
        cmd_ss << x->String();
        cmd_ss << "\n";
    }
    auto const& empty_map_exp = Expression::kEmptyMapExpr;
    auto env_exp = desc->ReadOptionalExpression("env", empty_map_exp);
    if (not env_exp) {
        return;
    }
    auto env_val = env_exp.Evaluate(
        param_config, string_fields_fcts, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating env:\n{}", msg), true);
        });
    if (not env_val) {
        return;
    }
    if (not env_val->IsMap()) {
        (*logger)(
            fmt::format("env has to evaluate to map of strings, but found {}",
                        env_val->ToString()),
            true);
        return;
    }
    for (auto const& [var_name, x] : env_val->Map()) {
        if (not x->IsString()) {
            (*logger)(fmt::format("env has to evaluate to map of strings, but "
                                  "found entry {}",
                                  x->ToString()),
                      true);
            return;
        }
    }

    auto sh_exp = desc->ReadOptionalExpression("sh -c", Expression::kEmptyList);
    if (not sh_exp) {
        return;
    }
    auto sh_val = sh_exp.Evaluate(
        param_config, string_fields_fcts, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating sh:\n{}", msg), true);
        });
    if (not sh_val) {
        return;
    }
    if (sh_val->IsNone()) {
        sh_val = Expression::kEmptyList;
    }
    if (not sh_val->IsList()) {
        (*logger)(fmt::format("sh has evaluate to list of strings or null, but "
                              "found {}",
                              sh_val->ToString()),
                  true);
        return;
    }
    for (auto const& entry : sh_val->List()) {
        if (not entry->IsString()) {
            (*logger)(fmt::format("sh has evaluate to list of strings or null, "
                                  "but found {}\nwith non-string entry {}",
                                  sh_val->ToString(),
                                  entry->ToString()),
                      true);
            return;
        }
    }
    static ExpressionPtr const kShC =
        Expression::FromJson(R"( ["sh", "-c"] )"_json);
    if (sh_val->List().empty()) {
        sh_val = kShC;
    }

    auto scale_exp =
        desc->ReadOptionalExpression("timeout scaling", Expression::kOne);
    if (not scale_exp) {
        return;
    }
    auto scale_val = scale_exp.Evaluate(
        param_config, string_fields_fcts, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating timeout scaling:\n{}", msg),
                      true);
        });
    if (not scale_val) {
        return;
    }
    if (not(scale_val->IsNumber() or scale_val->IsNone())) {
        (*logger)(fmt::format("timeout scaling has evaluate to a number (or "
                              "null for default), but found {}",
                              scale_val->ToString()),
                  true);
        return;
    }

    auto props_exp = desc->ReadOptionalExpression("execution properties",
                                                  Expression::kEmptyMapExpr);
    if (not props_exp) {
        return;
    }
    auto props_val = props_exp.Evaluate(
        param_config, string_fields_fcts, [&logger](auto const& msg) {
            (*logger)(
                fmt::format("While evaluating execution properties:\n{}", msg),
                true);
        });
    if (not props_val) {
        return;
    }
    if (props_val->IsNone()) {
        props_val = Expression::kEmptyMap;
    }
    if (not props_val->IsMap()) {
        (*logger)(fmt::format("execution properties has to evaluate to a map "
                              "(or null for default), but found {}",
                              props_val->ToString()),
                  true);
        return;
    }
    for (auto const& [prop_name, prop_val] : props_val->Map()) {
        if (not prop_val->IsString()) {
            (*logger)(
                fmt::format("execution properties has to evaluate to a map (or "
                            "null for default), but found {} for key {}",
                            nlohmann::json(prop_name).dump(),
                            prop_val->ToString()),
                true);
            return;
        }
    }

    // Construct inputs; in case of conflicts, artifacts take precedence
    // over runfiles.
    auto inputs = ExpressionPtr{Expression::map_t{}};
    for (auto const& dep : dependency_values) {
        inputs = ExpressionPtr{Expression::map_t{inputs, (*dep)->RunFiles()}};
    }
    for (auto const& dep : dependency_values) {
        inputs = ExpressionPtr{Expression::map_t{inputs, (*dep)->Artifacts()}};
    }

    std::vector<std::string> argv{};
    argv.reserve(sh_val->List().size() + 1);
    for (auto const& entry : sh_val->List()) {
        argv.emplace_back(entry->String());
    }
    argv.emplace_back(cmd_ss.str());

    // Construct our single action, and its artifacts
    auto action = BuildMaps::Target::Utils::createAction(
        outs,
        out_dirs,
        argv,
        env_val,
        std::nullopt,
        false,
        scale_val->IsNumber() ? scale_val->Number() : 1.0,
        props_val,
        inputs);
    auto action_identifier = action->Id();
    Expression::map_t::underlying_map_t artifacts;
    for (const auto& container : {outs, out_dirs}) {
        for (const auto& path : container) {
            artifacts.emplace(
                path,
                ExpressionPtr{ArtifactDescription{
                    action_identifier, std::filesystem::path{path}}});
        }
    }

    auto const& empty_map = Expression::kEmptyMap;
    auto result = std::make_shared<AnalysedTarget const>(
        TargetResult{
            .artifact_stage = ExpressionPtr{Expression::map_t{artifacts}},
            .provides = empty_map,
            .runfiles = empty_map},
        std::vector<ActionDescription::Ptr>{action},
        std::vector<std::string>{},
        std::vector<Tree::Ptr>{},
        std::move(effective_vars),
        std::move(tainted),
        std::move(deps_info));

    result = result_map->Add(key.target, effective_conf, std::move(result));
    (*setter)(std::move(result));
}

void GenericRule(
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*> result_map) {
    auto desc = BuildMaps::Base::FieldReader::CreatePtr(
        desc_json, key.target, "generic target", logger);
    desc->ExpectFields(kGenericRuleFields);
    auto param_vars = desc->ReadStringList("arguments_config");
    if (not param_vars) {
        return;
    }
    auto param_config = key.config.Prune(*param_vars);
    auto const& empty_list = Expression::kEmptyList;
    auto deps_exp = desc->ReadOptionalExpression("deps", empty_list);
    if (not deps_exp) {
        return;
    }
    auto deps_value =
        deps_exp.Evaluate(param_config, {}, [&logger](auto const& msg) {
            (*logger)(fmt::format("While evaluating deps:\n{}", msg), true);
        });
    if (not deps_value->IsList()) {
        (*logger)(fmt::format("Expected deps to evaluate to a list of targets, "
                              "but found {}",
                              deps_value->ToString()),
                  true);
        return;
    }
    std::vector<BuildMaps::Target::ConfiguredTarget> dependency_keys;
    std::vector<BuildMaps::Target::ConfiguredTarget> transition_keys;
    dependency_keys.reserve(deps_value->List().size());
    transition_keys.reserve(deps_value->List().size());
    auto empty_transition = Configuration{Expression::kEmptyMap};
    for (auto const& dep_name : deps_value->List()) {
        auto dep_target = BuildMaps::Base::ParseEntityNameFromExpression(
            dep_name,
            key.target,
            repo_config,
            [&logger, &dep_name](std::string const& parse_err) {
                (*logger)(fmt::format("Parsing dep entry {} failed with:\n{}",
                                      dep_name->ToString(),
                                      parse_err),
                          true);
            });
        if (not dep_target) {
            return;
        }
        dependency_keys.emplace_back(
            BuildMaps::Target::ConfiguredTarget{*dep_target, key.config});
        transition_keys.emplace_back(
            BuildMaps::Target::ConfiguredTarget{*dep_target, empty_transition});
    }
    (*subcaller)(
        dependency_keys,
        [transition_keys = std::move(transition_keys),
         desc,
         setter,
         logger,
         key,
         repo_config,
         result_map](auto const& values) {
            GenericRuleWithDeps(transition_keys,
                                values,
                                desc,
                                key,
                                repo_config,
                                setter,
                                logger,
                                result_map);
        },
        logger);
}

void ConfigureRule(
    const nlohmann::json& desc_json,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    auto desc = BuildMaps::Base::FieldReader::CreatePtr(
        desc_json, key.target, "configure target", logger);
    desc->ExpectFields(kConfigureRuleFields);
    auto param_vars = desc->ReadStringList("arguments_config");
    if (not param_vars) {
        return;
    }
    auto param_config = key.config.Prune(*param_vars);

    auto configured_target_name_exp = desc->ReadExpression("target");
    if (not configured_target_name_exp) {
        return;
    }
    auto configured_target_name = configured_target_name_exp.Evaluate(
        param_config, {}, [logger](std::string const& msg) {
            (*logger)(
                fmt::format("Evaluating 'target' failed with error:\n{}", msg),
                true);
        });
    if (not configured_target_name) {
        return;
    }
    auto configured_target = BuildMaps::Base::ParseEntityNameFromExpression(
        configured_target_name,
        key.target,
        repo_config,
        [&logger, &configured_target_name](std::string const& parse_err) {
            (*logger)(fmt::format("Parsing target name {} failed with:\n{}",
                                  configured_target_name->ToString(),
                                  parse_err),
                      true);
        });
    if (not configured_target) {
        return;
    }

    // Compute and verify taintedness
    auto tainted = std::set<std::string>{};
    auto got_tainted = BuildMaps::Target::Utils::getTainted(
        &tainted,
        param_config,
        desc->ReadOptionalExpression("tainted", Expression::kEmptyList),
        logger);
    if (not got_tainted) {
        return;
    }

    auto eval_config =
        desc->ReadOptionalExpression("config", Expression::kEmptyMapExpr);
    if (not eval_config->IsMap()) {
        (*logger)(fmt::format("eval_config has to be an expr, but found {}",
                              eval_config->ToString()),
                  true);
        return;
    }
    eval_config = eval_config.Evaluate(
        param_config, {}, [logger](std::string const& msg) {
            (*logger)(
                fmt::format("Evaluating 'config' failed with error:\n{}", msg),
                true);
        });
    if (not eval_config) {
        return;
    }
    if (not eval_config->IsMap()) {
        (*logger)(fmt::format("'config' must evaluate to map, but found {}",
                              eval_config->ToString()),
                  true);
        return;
    }

    auto target_config = key.config.Update(eval_config);
    auto target_to_configure = BuildMaps::Target::ConfiguredTarget{
        std::move(*configured_target), std::move(target_config)};

    (*subcaller)(
        {std::move(target_to_configure)},
        [setter,
         logger,
         vars = std::move(*param_vars),
         result_map,
         transition = Configuration{std::move(eval_config)},
         tainted = std::move(tainted),
         key](auto const& values) {
            auto& configured_target = *values[0];
            if (not std::includes(tainted.begin(),
                                  tainted.end(),
                                  configured_target->Tainted().begin(),
                                  configured_target->Tainted().end())) {
                (*logger)(
                    "Not tainted with all strings the dependencies are tainted "
                    "with",
                    true);
                return;
            }

            std::unordered_set<std::string> vars_set{};
            for (auto const& x : configured_target->Vars()) {
                if (not transition.VariableFixed(x)) {
                    vars_set.insert(x);
                }
            }
            vars_set.insert(vars.begin(), vars.end());
            auto effective_conf = key.config.Prune(vars_set);

            auto deps_info = TargetGraphInformation{
                std::make_shared<BuildMaps::Target::ConfiguredTarget>(
                    BuildMaps::Target::ConfiguredTarget{key.target,
                                                        effective_conf}),
                {configured_target->GraphInformation().Node()},
                {},
                {}};

            auto analysis_result = std::make_shared<AnalysedTarget const>(
                configured_target->Result(),
                std::vector<ActionDescription::Ptr>{},
                std::vector<std::string>{},
                std::vector<Tree::Ptr>{},
                std::move(vars_set),
                std::set<std::string>{},
                std::move(deps_info));
            analysis_result = result_map->Add(key.target,
                                              std::move(effective_conf),
                                              std::move(analysis_result));
            (*setter)(std::move(analysis_result));
        },
        logger);
}

auto const kBuiltIns = std::unordered_map<
    std::string,
    std::function<void(
        const nlohmann::json&,
        const BuildMaps::Target::ConfiguredTarget&,
        [[maybe_unused]] const gsl::not_null<RepositoryConfig*>&,
        const BuildMaps::Target::TargetMap::SubCallerPtr&,
        const BuildMaps::Target::TargetMap::SetterPtr&,
        const BuildMaps::Target::TargetMap::LoggerPtr&,
        const gsl::not_null<BuildMaps::Target::ResultTargetMap*>)>>{
    {"export", ExportRule},
    {"file_gen", FileGenRule},
    {"tree", TreeRule},
    {"symlink", SymlinkRule},
    {"generic", GenericRule},
    {"install", InstallRule},
    {"configure", ConfigureRule}};
}  // namespace

namespace BuildMaps::Target {

auto IsBuiltInRule(nlohmann::json const& rule_type) -> bool {

    if (not rule_type.is_string()) {
        // Names for built-in rules are always strings
        return false;
    }
    auto rule_name = rule_type.get<std::string>();
    return kBuiltIns.contains(rule_name);
}

auto HandleBuiltin(
    const nlohmann::json& rule_type,
    const nlohmann::json& desc,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<RepositoryConfig*>& repo_config,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map)
    -> bool {
    if (not rule_type.is_string()) {
        // Names for built-in rules are always strings
        return false;
    }
    auto rule_name = rule_type.get<std::string>();
    auto it = kBuiltIns.find(rule_name);
    if (it == kBuiltIns.end()) {
        return false;
    }
    auto target_logger = std::make_shared<BuildMaps::Target::TargetMap::Logger>(
        [logger, rule_name, key](auto msg, auto fatal) {
            (*logger)(fmt::format("While evaluating {} target {}:\n{}",
                                  rule_name,
                                  key.target.ToString(),
                                  msg),
                      fatal);
        });
    (it->second)(
        desc, key, repo_config, subcaller, setter, target_logger, result_map);
    return true;
}
}  // namespace BuildMaps::Target
