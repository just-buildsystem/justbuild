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

#include "src/buildtool/build_engine/target_map/target_map.hpp"

#ifdef __unix__
#include <fnmatch.h>
#else
#error "Non-unix is not supported yet"
#endif

#include <algorithm>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/build_engine/analysed_target/target_graph_information.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/expression_function.hpp"
#include "src/buildtool/build_engine/base_maps/field_reader.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/build_engine/base_maps/user_rule.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/build_engine/expression/linked_map.hpp"
#include "src/buildtool/build_engine/expression/target_node.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/build_engine/target_map/built_in_rules.hpp"
#include "src/buildtool/build_engine/target_map/utils.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/common/tree.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/path.hpp"
#include "src/utils/cpp/vector.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#endif  // BOOTSTRAP_BUILD_TOOL

namespace {

using namespace std::string_literals;

[[nodiscard]] auto ReadActionOutputExpr(ExpressionPtr const& out_exp,
                                        std::string const& field_name)
    -> ActionDescription::outputs_t {
    if (not out_exp->IsList()) {
        throw Evaluator::EvaluationError{
            fmt::format("{} has to be a list of strings, but found {}",
                        field_name,
                        out_exp->ToString())};
    }
    ActionDescription::outputs_t outputs;
    outputs.reserve(out_exp->List().size());
    for (auto const& out_path : out_exp->List()) {
        if (not out_path->IsString()) {
            throw Evaluator::EvaluationError{
                fmt::format("{} has to be a list of strings, but found {}",
                            field_name,
                            out_exp->ToString())};
        }
        outputs.emplace_back(out_path->String());
    }
    return outputs;
}

struct TargetData {
    using Ptr = std::shared_ptr<TargetData>;

    std::vector<std::string> target_vars;
    std::unordered_map<std::string, ExpressionPtr> config_exprs;
    std::unordered_map<std::string, ExpressionPtr> string_exprs;
    std::unordered_map<std::string, ExpressionPtr> target_exprs;
    ExpressionPtr tainted_expr;
    bool parse_target_names{};

    TargetData(std::vector<std::string> target_vars,
               std::unordered_map<std::string, ExpressionPtr> config_exprs,
               std::unordered_map<std::string, ExpressionPtr> string_exprs,
               std::unordered_map<std::string, ExpressionPtr> target_exprs,
               ExpressionPtr tainted_expr,
               bool parse_target_names)
        : target_vars{std::move(target_vars)},
          config_exprs{std::move(config_exprs)},
          string_exprs{std::move(string_exprs)},
          target_exprs{std::move(target_exprs)},
          tainted_expr{std::move(tainted_expr)},
          parse_target_names{parse_target_names} {}

    [[nodiscard]] static auto FromFieldReader(
        BuildMaps::Base::UserRulePtr const& rule,
        BuildMaps::Base::FieldReader::Ptr const& desc) -> TargetData::Ptr {
        desc->ExpectFields(rule->ExpectedFields());

        auto target_vars = desc->ReadStringList("arguments_config");
        auto tainted_expr =
            desc->ReadOptionalExpression("tainted", Expression::kEmptyList);

        auto convert_to_exprs =
            [&desc](gsl::not_null<
                        std::unordered_map<std::string, ExpressionPtr>*> const&
                        expr_map,
                    std::vector<std::string> const& field_names) -> bool {
            for (auto const& field_name : field_names) {
                auto expr = desc->ReadOptionalExpression(
                    field_name, Expression::kEmptyList);
                if (not expr) {
                    return false;
                }
                expr_map->emplace(field_name, std::move(expr));
            }
            return true;
        };

        std::unordered_map<std::string, ExpressionPtr> config_exprs;
        std::unordered_map<std::string, ExpressionPtr> string_exprs;
        std::unordered_map<std::string, ExpressionPtr> target_exprs;
        if (target_vars and tainted_expr and
            convert_to_exprs(&config_exprs, rule->ConfigFields()) and
            convert_to_exprs(&string_exprs, rule->StringFields()) and
            convert_to_exprs(&target_exprs, rule->TargetFields())) {
            return std::make_shared<TargetData>(std::move(*target_vars),
                                                std::move(config_exprs),
                                                std::move(string_exprs),
                                                std::move(target_exprs),
                                                std::move(tainted_expr),
                                                /*parse_target_names=*/true);
        }
        return nullptr;
    }

    [[nodiscard]] static auto FromTargetNode(
        BuildMaps::Base::UserRulePtr const& rule,
        TargetNode::Abstract const& node,
        ExpressionPtr const& rule_map,
        gsl::not_null<AsyncMapConsumerLoggerPtr> const& logger)
        -> TargetData::Ptr {

        auto const& string_fields = node.string_fields->Map();
        auto const& target_fields = node.target_fields->Map();

        std::unordered_map<std::string, ExpressionPtr> config_exprs;
        std::unordered_map<std::string, ExpressionPtr> string_exprs;
        std::unordered_map<std::string, ExpressionPtr> target_exprs;

        for (auto const& field_name : rule->ConfigFields()) {
            if (target_fields.Find(field_name)) {
                (*logger)(
                    fmt::format(
                        "Expected config field '{}' in string_fields of "
                        "abstract node type '{}', and not in target_fields",
                        field_name,
                        node.node_type),
                    /*fatal=*/true);
                return nullptr;
            }
            auto const& config_expr = *(string_fields.Find(field_name)
                                            .value_or(&Expression::kEmptyList));
            config_exprs.emplace(field_name, config_expr);
        }

        for (auto const& field_name : rule->StringFields()) {
            if (target_fields.Find(field_name)) {
                (*logger)(
                    fmt::format(
                        "Expected string field '{}' in string_fields of "
                        "abstract node type '{}', and not in target_fields",
                        field_name,
                        node.node_type),
                    /*fatal=*/true);
                return nullptr;
            }
            auto const& string_expr = *(string_fields.Find(field_name)
                                            .value_or(&Expression::kEmptyList));
            string_exprs.emplace(field_name, string_expr);
        }

        for (auto const& field_name : rule->TargetFields()) {
            if (string_fields.Find(field_name)) {
                (*logger)(
                    fmt::format(
                        "Expected target field '{}' in target_fields of "
                        "abstract node type '{}', and not in string_fields",
                        field_name,
                        node.node_type),
                    /*fatal=*/true);
                return nullptr;
            }
            auto const& target_expr = *(target_fields.Find(field_name)
                                            .value_or(&Expression::kEmptyList));
            auto const& nodes = target_expr->List();
            Expression::list_t targets{};
            targets.reserve(nodes.size());
            for (auto const& node_expr : nodes) {
                targets.emplace_back(BuildMaps::Base::EntityName{
                    BuildMaps::Base::AnonymousTarget{
                        .rule_map = rule_map, .target_node = node_expr}});
            }
            target_exprs.emplace(field_name, targets);
        }

        return std::make_shared<TargetData>(std::vector<std::string>{},
                                            std::move(config_exprs),
                                            std::move(string_exprs),
                                            std::move(target_exprs),
                                            Expression::kEmptyList,
                                            /*parse_target_names=*/false);
    }
};

auto NameTransitionedDeps(
    const BuildMaps::Target::ConfiguredTarget& transitioned_target,
    const AnalysedTargetPtr& analysis,
    const Configuration& effective_conf) -> std::string {
    auto conf = effective_conf.Update(transitioned_target.config.Expr())
                    .Prune(analysis->Vars());
    return BuildMaps::Target::ConfiguredTarget{transitioned_target.target, conf}
        .ToShortString(Evaluator::GetExpressionLogLimit());
}

// Check if an object is contained an expression; to avoid tree-unfolding
// the expression, we need to cache the values already computed.
auto ExpressionContainsObject(std::unordered_map<ExpressionPtr, bool>* map,
                              const ExpressionPtr& object,
                              const ExpressionPtr& exp) {
    auto lookup = map->find(exp);
    if (lookup != map->end()) {
        return lookup->second;
    }
    auto result = false;
    if (exp == object) {
        result = true;
    }
    else if (exp->IsList()) {
        for (auto const& entry : exp->List()) {
            if (ExpressionContainsObject(map, object, entry)) {
                result = true;
                break;
            }
        }
    }
    else if (exp->IsMap()) {
        for (auto const& [k, v] : exp->Map()) {
            if (ExpressionContainsObject(map, object, v)) {
                result = true;
                break;
            }
        }
    }
    map->insert({exp, result});
    return result;
}

auto ListDependencies(
    const ExpressionPtr& object,
    const std::unordered_map<BuildMaps::Target::ConfiguredTarget,
                             AnalysedTargetPtr>& deps_by_transition,
    const Configuration& effective_conf) -> std::string {
    std::stringstream deps{};
    std::unordered_map<ExpressionPtr, bool> contains_object{};
    for (auto const& [transition_target, analysis] : deps_by_transition) {
        for (auto const& [path, value] : analysis->Artifacts().Map()) {
            if (value == object) {
                deps << fmt::format(
                    "\n - {}, artifact at {}",
                    NameTransitionedDeps(
                        transition_target, analysis, effective_conf),
                    nlohmann::json(path).dump());
            }
        }
        for (auto const& [path, value] : analysis->RunFiles().Map()) {
            if (value == object) {
                deps << fmt::format(
                    "\n - {}, runfile at {}",
                    NameTransitionedDeps(
                        transition_target, analysis, effective_conf),
                    nlohmann::json(path).dump());
            }
        }
        if (ExpressionContainsObject(
                &contains_object, object, analysis->Provides())) {
            deps << fmt::format(
                "\n - {}, in provided data",
                NameTransitionedDeps(
                    transition_target, analysis, effective_conf));
        }
    }

    return deps.str();
}

void withDependencies(
    const gsl::not_null<AnalyseContext*>& context,
    const std::vector<BuildMaps::Target::ConfiguredTarget>& transition_keys,
    const std::vector<AnalysedTargetPtr const*>& dependency_values,
    std::size_t declared_count,
    std::size_t declared_and_implicit_count,
    const BuildMaps::Base::UserRulePtr& rule,
    const TargetData::Ptr& data,
    const BuildMaps::Target::ConfiguredTarget& key,
    std::unordered_map<std::string, ExpressionPtr> params,  // NOLINT
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map) {
    // Associate dependency keys with values
    std::unordered_map<BuildMaps::Target::ConfiguredTarget, AnalysedTargetPtr>
        deps_by_transition;
    deps_by_transition.reserve(transition_keys.size());
    ExpectsAudit(transition_keys.size() == dependency_values.size());
    for (std::size_t i = 0; i < transition_keys.size(); ++i) {
        deps_by_transition.emplace(transition_keys[i], *dependency_values[i]);
    }

    // Compute the effective dependency on config variables
    std::unordered_set<std::string> effective_vars;
    auto const& param_vars = data->target_vars;
    effective_vars.insert(param_vars.begin(), param_vars.end());
    auto const& config_vars = rule->ConfigVars();
    effective_vars.insert(config_vars.begin(), config_vars.end());
    for (auto const& [transition, target] : deps_by_transition) {
        for (auto const& x : target->Vars()) {
            if (not transition.config.VariableFixed(x)) {
                effective_vars.insert(x);
            }
        }
    }
    auto effective_conf = key.config.Prune(effective_vars);
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> declared_deps{};
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> implicit_deps{};
    std::vector<BuildMaps::Target::ConfiguredTargetPtr> anonymous_deps{};
    ExpectsAudit(declared_count <= declared_and_implicit_count);
    ExpectsAudit(declared_and_implicit_count <= dependency_values.size());
    auto fill_target_graph = [&dependency_values](std::size_t const a,
                                                  std::size_t const b,
                                                  auto* deps) {
        std::transform(
            dependency_values.begin() + static_cast<std::int64_t>(a),
            dependency_values.begin() + static_cast<std::int64_t>(b),
            std::back_inserter(*deps),
            [](auto dep) { return (*(dep))->GraphInformation().Node(); });
    };

    fill_target_graph(0, declared_count, &declared_deps);
    fill_target_graph(
        declared_count, declared_and_implicit_count, &implicit_deps);
    fill_target_graph(
        declared_and_implicit_count, dependency_values.size(), &anonymous_deps);
    auto deps_info = TargetGraphInformation{
        std::make_shared<BuildMaps::Target::ConfiguredTarget>(
            BuildMaps::Target::ConfiguredTarget{.target = key.target,
                                                .config = effective_conf}),
        declared_deps,
        implicit_deps,
        anonymous_deps};

    // Compute and verify taintedness
    auto tainted = std::set<std::string>{};
    auto got_tainted = BuildMaps::Target::Utils::getTainted(
        &tainted, key.config.Prune(param_vars), data->tainted_expr, logger);
    if (not got_tainted) {
        return;
    }
    tainted.insert(rule->Tainted().begin(), rule->Tainted().end());
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

    // Compute implied export targets
    std::set<std::string> implied_export{};
    for (auto const& dep : dependency_values) {
        implied_export.insert((*dep)->ImpliedExport().begin(),
                              (*dep)->ImpliedExport().end());
    }

    // Evaluate string parameters
    auto string_fields_fcts =
        FunctionMap::MakePtr(FunctionMap::underlying_map_t{
            {"outs",
             [&deps_by_transition, &key, context](
                 auto&& eval, auto const& expr, auto const& env) {
                 return BuildMaps::Target::Utils::keys_expr(
                     BuildMaps::Target::Utils::obtainTargetByName(
                         eval,
                         expr,
                         env,
                         key.target,
                         context->repo_config,
                         deps_by_transition)
                         ->Artifacts());
             }},
            {"runfiles",
             [&deps_by_transition, &key, context](
                 auto&& eval, auto const& expr, auto const& env) {
                 return BuildMaps::Target::Utils::keys_expr(
                     BuildMaps::Target::Utils::obtainTargetByName(
                         eval,
                         expr,
                         env,
                         key.target,
                         context->repo_config,
                         deps_by_transition)
                         ->RunFiles());
             }}});
    auto param_config = key.config.Prune(param_vars);
    params.reserve(params.size() + rule->StringFields().size());
    for (auto const& field_name : rule->StringFields()) {
        auto const& field_exp = data->string_exprs[field_name];
        auto field_value = field_exp.Evaluate(
            param_config,
            string_fields_fcts,
            [&logger, &field_name](auto const& msg) {
                (*logger)(fmt::format("While evaluating string field {}:\n{}",
                                      field_name,
                                      msg),
                          true);
            });
        if (not field_value) {
            return;
        }
        if (not field_value->IsList()) {
            (*logger)(fmt::format("String field {} should be a list of "
                                  "strings, but found {}",
                                  field_name,
                                  field_value->ToString()),
                      true);
            return;
        }
        for (auto const& entry : field_value->List()) {
            if (not entry->IsString()) {
                (*logger)(fmt::format("String field {} should be a list of "
                                      "strings, but found entry {}",
                                      field_name,
                                      entry->ToString()),
                          true);
                return;
            }
        }
        params.emplace(field_name, std::move(field_value));
    }

    // Evaluate main expression
    auto expression_config = key.config.Prune(config_vars);
    std::vector<ActionDescription::Ptr> actions{};
    std::vector<std::string> blobs{};
    std::vector<Tree::Ptr> trees{};
    auto main_exp_fcts = FunctionMap::MakePtr(
        {{"FIELD",
          [&params](auto&& eval, auto const& expr, auto const& env) {
              auto name = eval(expr["name"], env);
              if (not name->IsString()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("FIELD argument 'name' should evaluate to a "
                                  "string, but got {}",
                                  name->ToString())};
              }
              auto it = params.find(name->String());
              if (it == params.end()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("FIELD '{}' unknown", name->String())};
              }
              return it->second;
          }},
         {"DEP_ARTIFACTS",
          [&deps_by_transition](
              auto&& eval, auto const& expr, auto const& env) {
              return BuildMaps::Target::Utils::obtainTarget(
                         eval, expr, env, deps_by_transition)
                  ->Artifacts();
          }},
         {"DEP_RUNFILES",
          [&deps_by_transition](
              auto&& eval, auto const& expr, auto const& env) {
              return BuildMaps::Target::Utils::obtainTarget(
                         eval, expr, env, deps_by_transition)
                  ->RunFiles();
          }},
         {"DEP_PROVIDES",
          [&deps_by_transition](
              auto&& eval, auto const& expr, auto const& env) {
              auto const& provided = BuildMaps::Target::Utils::obtainTarget(
                                         eval, expr, env, deps_by_transition)
                                         ->Provides();
              auto provider = eval(expr["provider"], env);
              auto provided_value = provided->At(provider->String());
              if (provided_value) {
                  return provided_value->get();
              }
              auto const& empty_list = Expression::kEmptyList;
              return eval(expr->Get("default", empty_list), env);
          }},
         {"ACTION",
          [&actions, &rule, &trees](
              auto&& eval, auto const& expr, auto const& env) {
              auto const& empty_map_exp = Expression::kEmptyMapExpr;
              auto inputs_exp = eval(expr->Get("inputs", empty_map_exp), env);
              if (not inputs_exp->IsMap()) {
                  throw Evaluator::EvaluationError{fmt::format(
                      "inputs has to be a map of artifacts, but found {}",
                      inputs_exp->ToString())};
              }
              for (auto const& [input_path, artifact] : inputs_exp->Map()) {
                  if (not artifact->IsArtifact()) {
                      throw Evaluator::EvaluationError{
                          fmt::format("inputs has to be a map of Artifacts, "
                                      "but found {} for {}",
                                      artifact->ToString(),
                                      input_path)};
                  }
              }
              auto conflict_or_normal =
                  BuildMaps::Target::Utils::artifacts_tree(inputs_exp);
              if (std::holds_alternative<std::string>(conflict_or_normal)) {
                  throw Evaluator::EvaluationError{
                      fmt::format("inputs conflict on path {}",
                                  std::get<std::string>(conflict_or_normal))};
              }
              inputs_exp = std::get<ExpressionPtr>(conflict_or_normal);
              auto conflict =
                  BuildMaps::Target::Utils::tree_conflict(inputs_exp);
              if (conflict) {
                  throw Evaluator::EvaluationError{
                      fmt::format("inputs conflicts on subtree {}", *conflict)};
              }

              Expression::map_t::underlying_map_t result;
              auto outputs = ReadActionOutputExpr(
                  eval(expr->Get("outs", Expression::list_t{}), env), "outs");
              auto output_dirs = ReadActionOutputExpr(
                  eval(expr->Get("out_dirs", Expression::list_t{}), env),
                  "out_dirs");
              if (outputs.empty() and output_dirs.empty()) {
                  throw Evaluator::EvaluationError{
                      "either outs or out_dirs must be specified for ACTION"};
              }
              ActionDescription::outputs_t outputs_norm{};
              ActionDescription::outputs_t output_dirs_norm{};
              outputs_norm.reserve(outputs.size());
              output_dirs_norm.reserve(output_dirs.size());
              std::for_each(
                  outputs.begin(), outputs.end(), [&outputs_norm](auto p) {
                      outputs_norm.emplace_back(ToNormalPath(p));
                  });
              std::for_each(output_dirs.begin(),
                            output_dirs.end(),
                            [&output_dirs_norm](auto p) {
                                output_dirs_norm.emplace_back(ToNormalPath(p));
                            });

              sort_and_deduplicate(&outputs_norm);
              sort_and_deduplicate(&output_dirs_norm);

              // find entries present on both fields
              std::vector<std::string> dups{};
              std::set_intersection(outputs_norm.begin(),
                                    outputs_norm.end(),
                                    output_dirs_norm.begin(),
                                    output_dirs_norm.end(),
                                    std::back_inserter(dups));
              if (not dups.empty()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("outs and out_dirs for ACTION must be "
                                  "disjoint. Found repeated entries:\n{}",
                                  nlohmann::json(dups).dump())};
              }

              std::vector<std::string> cmd;
              auto cmd_exp = eval(expr->Get("cmd", Expression::list_t{}), env);
              if (not cmd_exp->IsList()) {
                  throw Evaluator::EvaluationError{fmt::format(
                      "cmd has to be a list of strings, but found {}",
                      cmd_exp->ToString())};
              }
              if (cmd_exp->List().empty()) {
                  throw Evaluator::EvaluationError{
                      "cmd must not be an empty list"};
              }
              cmd.reserve(cmd_exp->List().size());
              for (auto const& arg : cmd_exp->List()) {
                  if (not arg->IsString()) {
                      throw Evaluator::EvaluationError{fmt::format(
                          "cmd has to be a list of strings, but found {}",
                          cmd_exp->ToString())};
                  }
                  cmd.emplace_back(arg->String());
              }
              auto cwd_exp =
                  eval(expr->Get("cwd", Expression::kEmptyString), env);
              if (not cwd_exp->IsString()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("cwd has to be a string, but found {}",
                                  cwd_exp->ToString())};
              }
              if (not PathIsNonUpwards(cwd_exp->String())) {
                  throw Evaluator::EvaluationError{fmt::format(
                      "cwd has to be a non-upwards relative path, but found {}",
                      cwd_exp->ToString())};
              }
              auto final_inputs = BuildMaps::Target::Utils::add_dir_for(
                  cwd_exp->String(), inputs_exp, &trees);
              auto env_exp = eval(expr->Get("env", empty_map_exp), env);
              if (not env_exp->IsMap()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("env has to be a map of string, but found {}",
                                  env_exp->ToString())};
              }
              for (auto const& [env_var, env_value] : env_exp->Map()) {
                  if (not env_value->IsString()) {
                      throw Evaluator::EvaluationError{fmt::format(
                          "env has to be a map of string, but found {}",
                          env_exp->ToString())};
                  }
              }
              auto may_fail_exp = expr->Get("may_fail", Expression::list_t{});
              if (not may_fail_exp->IsList()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("may_fail has to be a list of "
                                  "strings, but found {}",
                                  may_fail_exp->ToString())};
              }
              for (auto const& entry : may_fail_exp->List()) {
                  if (not entry->IsString()) {
                      throw Evaluator::EvaluationError{
                          fmt::format("may_fail has to be a list of "
                                      "strings, but found {}",
                                      may_fail_exp->ToString())};
                  }
                  if (rule->Tainted().find(entry->String()) ==
                      rule->Tainted().end()) {
                      throw Evaluator::EvaluationError{
                          fmt::format("may_fail contains entry {} the rule "
                                      "is not tainted with",
                                      entry->ToString())};
                  }
              }
              std::optional<std::string> may_fail = std::nullopt;
              if (not may_fail_exp->List().empty()) {
                  auto fail_msg =
                      eval(expr->Get("fail_message", "action failed"s), env);
                  if (not fail_msg->IsString()) {
                      throw Evaluator::EvaluationError{
                          fmt::format("fail_message has to evaluate to a "
                                      "string, but got {}",
                                      fail_msg->ToString())};
                  }
                  may_fail = std::optional{fail_msg->String()};
              }
              auto no_cache_exp = expr->Get("no_cache", Expression::list_t{});
              if (not no_cache_exp->IsList()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("no_cache has to be a list of"
                                  "strings, but found {}",
                                  no_cache_exp->ToString())};
              }
              for (auto const& entry : no_cache_exp->List()) {
                  if (not entry->IsString()) {
                      throw Evaluator::EvaluationError{
                          fmt::format("no_cache has to be a list of"
                                      "strings, but found {}",
                                      no_cache_exp->ToString())};
                  }
                  if (rule->Tainted().find(entry->String()) ==
                      rule->Tainted().end()) {
                      throw Evaluator::EvaluationError{
                          fmt::format("no_cache contains entry {} the rule "
                                      "is not tainted with",
                                      entry->ToString())};
                  }
              }
              bool no_cache = not no_cache_exp->List().empty();
              auto timeout_scale_exp =
                  eval(expr->Get("timeout scaling", Expression::kOne), env);
              if (not(timeout_scale_exp->IsNumber() or
                      timeout_scale_exp->IsNone())) {
                  throw Evaluator::EvaluationError{
                      fmt::format("timeout scaling has to be number (or null "
                                  "for default), but found {}",
                                  timeout_scale_exp->ToString())};
              }
              auto execution_properties = eval(
                  expr->Get("execution properties", Expression::kEmptyMapExpr),
                  env);
              if (execution_properties->IsNone()) {
                  execution_properties = Expression::kEmptyMap;
              }
              if (not(execution_properties->IsMap())) {
                  throw Evaluator::EvaluationError{
                      fmt::format("execution properties has to be a map of "
                                  "strings (or null for empty), but found {}",
                                  execution_properties->ToString())};
              }
              for (auto const& [prop_name, prop_value] :
                   execution_properties->Map()) {
                  if (not prop_value->IsString()) {
                      throw Evaluator::EvaluationError{fmt::format(
                          "execution properties has to be a map of "
                          "strings (or null for empty), but found {}",
                          execution_properties->ToString())};
                  }
              }
              auto action = BuildMaps::Target::Utils::createAction(
                  outputs_norm,
                  output_dirs_norm,
                  std::move(cmd),
                  cwd_exp->String(),
                  env_exp,
                  may_fail,
                  no_cache,
                  timeout_scale_exp->IsNumber() ? timeout_scale_exp->Number()
                                                : 1.0,
                  execution_properties,
                  final_inputs);
              auto action_id = action->Id();
              actions.emplace_back(std::move(action));
              for (auto const& out : outputs) {
                  result.emplace(
                      out,
                      ExpressionPtr{ArtifactDescription::CreateAction(
                          action_id, ToNormalPath(out))});
              }
              for (auto const& out : output_dirs) {
                  result.emplace(
                      out,
                      ExpressionPtr{ArtifactDescription::CreateAction(
                          action_id, ToNormalPath(out))});
              }

              return ExpressionPtr{Expression::map_t{result}};
          }},
         {"BLOB",
          [&blobs, context](auto&& eval, auto const& expr, auto const& env) {
              auto data = eval(expr->Get("data", ""s), env);
              if (not data->IsString()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("BLOB data has to be a string, but got {}",
                                  data->ToString())};
              }
              blobs.emplace_back(data->String());
              return ExpressionPtr{ArtifactDescription::CreateKnown(
                  ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                      context->storage->GetHashFunction(), data->String()),
                  ObjectType::File)};
          }},

         {"SYMLINK",
          [&blobs, context](auto&& eval, auto const& expr, auto const& env) {
              auto data = eval(expr->Get("data", ""s), env);
              if (not data->IsString()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("SYMLINK data has to be a string, but got {}",
                                  data->ToString())};
              }
              if (not PathIsNonUpwards(data->String())) {
                  throw Evaluator::EvaluationError{fmt::format(
                      "SYMLINK data has to be non-upwards relative, but got {}",
                      data->ToString())};
              }
              blobs.emplace_back(data->String());

              return ExpressionPtr{ArtifactDescription::CreateKnown(
                  ArtifactDigestFactory::HashDataAs<ObjectType::Symlink>(
                      context->storage->GetHashFunction(), data->String()),
                  ObjectType::Symlink)};
          }},

         {"TREE",
          [&trees](auto&& eval, auto const& expr, auto const& env) {
              auto val = eval(expr->Get("$1", Expression::kEmptyMapExpr), env);
              if (not val->IsMap()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("TREE argument has to be a map of artifacts, "
                                  "but found {}",
                                  val->ToString())};
              }
              std::unordered_map<std::string, ArtifactDescription> artifacts;
              artifacts.reserve(val->Map().size());
              for (auto const& [input_path, artifact] : val->Map()) {
                  if (not artifact->IsArtifact()) {
                      throw Evaluator::EvaluationError{fmt::format(
                          "TREE argument has to be a map of artifacts, "
                          "but found {} for {}",
                          artifact->ToString(),
                          input_path)};
                  }
                  auto norm_path =
                      ToNormalPath(std::filesystem::path{input_path});
                  artifacts.emplace(std::move(norm_path), artifact->Artifact());
              }
              auto conflict = BuildMaps::Target::Utils::tree_conflict(val);
              if (conflict) {
                  throw Evaluator::EvaluationError{
                      fmt::format("TREE conflicts on subtree {}", *conflict)};
              }
              auto tree = std::make_shared<Tree>(std::move(artifacts));
              auto tree_id = tree->Id();
              trees.emplace_back(std::move(tree));
              return ExpressionPtr{ArtifactDescription::CreateTree(tree_id)};
          }},
         {"VALUE_NODE",
          [](auto&& eval, auto const& expr, auto const& env) {
              auto val = eval(expr->Get("$1", Expression::kNone), env);
              if (not val->IsResult()) {
                  throw Evaluator::EvaluationError{
                      "argument '$1' for VALUE_NODE not a RESULT type."};
              }
              return ExpressionPtr{TargetNode{std::move(val)}};
          }},
         {"ABSTRACT_NODE",
          [](auto&& eval, auto const& expr, auto const& env) {
              auto type = eval(expr->Get("node_type", Expression::kNone), env);
              if (not type->IsString()) {
                  throw Evaluator::EvaluationError{
                      "argument 'node_type' for ABSTRACT_NODE not a string."};
              }
              auto string_fields = eval(
                  expr->Get("string_fields", Expression::kEmptyMapExpr), env);
              if (not string_fields->IsMap()) {
                  throw Evaluator::EvaluationError{
                      "argument 'string_fields' for ABSTRACT_NODE not a map."};
              }
              auto target_fields = eval(
                  expr->Get("target_fields", Expression::kEmptyMapExpr), env);
              if (not target_fields->IsMap()) {
                  throw Evaluator::EvaluationError{
                      "argument 'target_fields' for ABSTRACT_NODE not a map."};
              }

              std::optional<std::string> dup_key{std::nullopt};
              auto check_entries =
                  [&dup_key](auto const& map,
                             auto const& type_check,
                             std::string const& fields_name,
                             std::string const& type_name,
                             std::optional<ExpressionPtr> const& disjoint_map =
                                 std::nullopt) {
                      for (auto const& [key, list] : map->Map()) {
                          if (not list->IsList()) {
                              throw Evaluator::EvaluationError{fmt::format(
                                  "value for key {} in argument '{}' for "
                                  "ABSTRACT_NODE is not a list.",
                                  key,
                                  fields_name)};
                          }
                          for (auto const& entry : list->List()) {
                              if (not type_check(entry)) {
                                  throw Evaluator::EvaluationError{fmt::format(
                                      "list entry for {} in argument '{}' for "
                                      "ABSTRACT_NODE is not a {}:\n{}",
                                      key,
                                      fields_name,
                                      type_name,
                                      entry->ToString())};
                              }
                          }
                          if (disjoint_map) {
                              if ((*disjoint_map)->Map().Find(key)) {
                                  dup_key = key;
                                  return;
                              }
                          }
                      }
                  };

              auto is_string = [](auto const& e) { return e->IsString(); };
              check_entries(string_fields,
                            is_string,
                            "string_fields",
                            "string",
                            target_fields);
              if (dup_key) {
                  throw Evaluator::EvaluationError{
                      fmt::format("string_fields and target_fields are not "
                                  "disjoint maps, found duplicate key: {}.",
                                  *dup_key)};
              }

              auto is_node = [](auto const& e) { return e->IsNode(); };
              check_entries(
                  target_fields, is_node, "target_fields", "target node");

              return ExpressionPtr{TargetNode{TargetNode::Abstract{
                  .node_type = type->String(),
                  .string_fields = std::move(string_fields),
                  .target_fields = std::move(target_fields)}}};
          }},
         {"RESULT", [](auto&& eval, auto const& expr, auto const& env) {
              auto const& empty_map_exp = Expression::kEmptyMapExpr;
              auto artifacts = eval(expr->Get("artifacts", empty_map_exp), env);
              auto runfiles = eval(expr->Get("runfiles", empty_map_exp), env);
              auto provides = eval(expr->Get("provides", empty_map_exp), env);
              if (not artifacts->IsMap()) {
                  throw Evaluator::EvaluationError{fmt::format(
                      "artifacts has to be a map of artifacts, but found {}",
                      artifacts->ToString())};
              }
              for (auto const& [path, entry] : artifacts->Map()) {
                  if (not entry->IsArtifact()) {
                      throw Evaluator::EvaluationError{
                          fmt::format("artifacts has to be a map of artifacts, "
                                      "but found {} for {}",
                                      entry->ToString(),
                                      path)};
                  }
              }
              auto artifacts_normal =
                  BuildMaps::Target::Utils::artifacts_tree(artifacts);
              if (std::holds_alternative<std::string>(artifacts_normal)) {
                  throw Evaluator::EvaluationError{
                      fmt::format("artifacts conflict on path {}",
                                  std::get<std::string>(artifacts_normal))};
              }
              artifacts = std::get<ExpressionPtr>(artifacts_normal);
              auto artifacts_conflict =
                  BuildMaps::Target::Utils::tree_conflict(artifacts);
              if (artifacts_conflict) {
                  throw Evaluator::EvaluationError{
                      fmt::format("artifacts conflicts on subtree {}",
                                  *artifacts_conflict)};
              }
              if (not runfiles->IsMap()) {
                  throw Evaluator::EvaluationError{fmt::format(
                      "runfiles has to be a map of artifacts, but found {}",
                      runfiles->ToString())};
              }
              for (auto const& [path, entry] : runfiles->Map()) {
                  if (not entry->IsArtifact()) {
                      throw Evaluator::EvaluationError{
                          fmt::format("runfiles has to be a map of artifacts, "
                                      "but found {} for {}",
                                      entry->ToString(),
                                      path)};
                  }
              }
              auto runfiles_normal =
                  BuildMaps::Target::Utils::artifacts_tree(runfiles);
              if (std::holds_alternative<std::string>(runfiles_normal)) {
                  throw Evaluator::EvaluationError{
                      fmt::format("runfiles conflict on path {}",
                                  std::get<std::string>(runfiles_normal))};
              }
              runfiles = std::get<ExpressionPtr>(runfiles_normal);
              auto runfiles_conflict =
                  BuildMaps::Target::Utils::tree_conflict(runfiles);
              if (runfiles_conflict) {
                  throw Evaluator::EvaluationError{fmt::format(
                      "runfiles conflicts on subtree {}", *runfiles_conflict)};
              }
              if (not provides->IsMap()) {
                  throw Evaluator::EvaluationError{
                      fmt::format("provides has to be a map, but found {}",
                                  provides->ToString())};
              }
              return ExpressionPtr{TargetResult{.artifact_stage = artifacts,
                                                .provides = provides,
                                                .runfiles = runfiles}};
          }}});

    std::function<std::string(ExpressionPtr)> annotate_object =
        [&deps_by_transition, &effective_conf](auto const& object) {
            if (not object->IsArtifact()) {
                // we only annotate artifacts
                return std::string{};
            }
            auto occurrences =
                ListDependencies(object, deps_by_transition, effective_conf);
            if (not occurrences.empty()) {
                return fmt::format(
                    "\nArtifact {} occurs in direct dependencies{}",
                    object->ToString(),
                    occurrences);
            }
            return fmt::format("\nArtifact {} unknown to direct dependencies",
                               object->ToString());
        };
    auto result = rule->Expression()->Evaluate(
        expression_config,
        main_exp_fcts,
        [logger](auto const& msg) {
            (*logger)(
                fmt::format("While evaluating defining expression of rule:\n{}",
                            msg),
                true);
        },
        annotate_object);
    if (not result) {
        return;
    }
    if (not result->IsResult()) {
        (*logger)(fmt::format("Defining expression should evaluate to a "
                              "RESULT, but got: {}",
                              result->ToString()),
                  true);
        return;
    }
    auto analysis_result =
        std::make_shared<AnalysedTarget const>(std::move(*result).Result(),
                                               std::move(actions),
                                               std::move(blobs),
                                               std::move(trees),
                                               std::move(effective_vars),
                                               std::move(tainted),
                                               std::move(implied_export),
                                               deps_info);
    analysis_result =
        result_map->Add(key.target, effective_conf, std::move(analysis_result));
    (*setter)(std::move(analysis_result));
}

[[nodiscard]] auto isTransition(
    const ExpressionPtr& ptr,
    std::function<void(std::string const&)> const& logger) -> bool {
    if (not ptr->IsList()) {
        logger(fmt::format("expected list, but got {}", ptr->ToString()));
        return false;
    }
    if (not std::all_of(ptr->List().begin(),
                        ptr->List().end(),
                        [](auto const& entry) { return entry->IsMap(); })) {
        logger(fmt::format("expected list of dicts, but found {}",
                           ptr->ToString()));
        return false;
    }

    return true;
}

void withRuleDefinition(
    const gsl::not_null<AnalyseContext*>& context,
    const BuildMaps::Base::UserRulePtr& rule,
    const TargetData::Ptr& data,
    const BuildMaps::Target::ConfiguredTarget& key,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*> result_map) {
    auto param_config = key.config.Prune(data->target_vars);

    // Evaluate the config_fields

    std::unordered_map<std::string, ExpressionPtr> params;
    params.reserve(rule->ConfigFields().size() + rule->TargetFields().size() +
                   rule->ImplicitTargetExps().size());
    for (auto const& field_name : rule->ConfigFields()) {
        auto const& field_expression = data->config_exprs[field_name];
        auto field_value = field_expression.Evaluate(
            param_config, {}, [&logger, &field_name](auto const& msg) {
                (*logger)(fmt::format("While evaluating config field {}:\n{}",
                                      field_name,
                                      msg),
                          true);
            });
        if (not field_value) {
            return;
        }
        if (not field_value->IsList()) {
            (*logger)(fmt::format("Config field {} should evaluate to a list "
                                  "of strings, but got{}",
                                  field_name,
                                  field_value->ToString()),
                      true);
            return;
        }
        for (auto const& entry : field_value->List()) {
            if (not entry->IsString()) {
                (*logger)(fmt::format("Config field {} should evaluate to a "
                                      "list of strings, but got{}",
                                      field_name,
                                      field_value->ToString()),
                          true);
                return;
            }
        }
        params.emplace(field_name, field_value);
    }

    // Evaluate config transitions

    auto config_trans_fcts = FunctionMap::MakePtr(
        "FIELD", [&params](auto&& eval, auto const& expr, auto const& env) {
            auto name = eval(expr["name"], env);
            if (not name->IsString()) {
                throw Evaluator::EvaluationError{
                    fmt::format("FIELD argument 'name' should evaluate to a "
                                "string, but got {}",
                                name->ToString())};
            }
            auto it = params.find(name->String());
            if (it == params.end()) {
                throw Evaluator::EvaluationError{
                    fmt::format("FIELD {} unknown", name->String())};
            }
            return it->second;
        });

    auto const& config_vars = rule->ConfigVars();
    auto expression_config = key.config.Prune(config_vars);

    std::unordered_map<std::string, ExpressionPtr> config_transitions;
    config_transitions.reserve(rule->TargetFields().size() +
                               rule->ImplicitTargets().size() +
                               rule->AnonymousDefinitions().size());
    for (auto const& target_field_name : rule->TargetFields()) {
        auto exp = rule->ConfigTransitions().at(target_field_name);
        auto transition_logger = [&logger,
                                  &target_field_name](auto const& msg) {
            (*logger)(
                fmt::format("While evaluating config transition for {}:\n{}",
                            target_field_name,
                            msg),
                true);
        };
        auto transition = exp->Evaluate(
            expression_config, config_trans_fcts, transition_logger);
        if (not transition) {
            return;
        }
        if (not isTransition(transition, transition_logger)) {
            return;
        }
        config_transitions.emplace(target_field_name, transition);
    }
    for (const auto& name_value : rule->ImplicitTargets()) {
        auto implicit_field_name = name_value.first;
        auto exp = rule->ConfigTransitions().at(implicit_field_name);
        auto transition_logger = [&logger,
                                  &implicit_field_name](auto const& msg) {
            (*logger)(fmt::format("While evaluating config transition for "
                                  "implicit {}:\n{}",
                                  implicit_field_name,
                                  msg),
                      true);
        };
        auto transition = exp->Evaluate(
            expression_config, config_trans_fcts, transition_logger);
        if (not transition) {
            return;
        }
        if (not isTransition(transition, transition_logger)) {
            return;
        }
        config_transitions.emplace(implicit_field_name, transition);
    }
    for (const auto& entry : rule->AnonymousDefinitions()) {
        auto const& anon_field_name = entry.first;
        auto exp = rule->ConfigTransitions().at(anon_field_name);
        auto transition_logger = [&logger, &anon_field_name](auto const& msg) {
            (*logger)(fmt::format("While evaluating config transition for "
                                  "anonymous {}:\n{}",
                                  anon_field_name,
                                  msg),
                      true);
        };
        auto transition = exp->Evaluate(
            expression_config, config_trans_fcts, transition_logger);
        if (not transition) {
            return;
        }
        if (not isTransition(transition, transition_logger)) {
            return;
        }
        config_transitions.emplace(anon_field_name, transition);
    }

    // Request dependencies

    std::unordered_map<std::string, std::vector<std::size_t>> anon_positions;
    anon_positions.reserve(rule->AnonymousDefinitions().size());
    for (auto const& [_, def] : rule->AnonymousDefinitions()) {
        anon_positions.emplace(def.target, std::vector<std::size_t>{});
    }

    std::vector<BuildMaps::Target::ConfiguredTarget> dependency_keys;
    std::vector<BuildMaps::Target::ConfiguredTarget> transition_keys;
    for (auto const& target_field_name : rule->TargetFields()) {
        auto const& deps_expression = data->target_exprs[target_field_name];
        auto deps_names = deps_expression.Evaluate(
            param_config, {}, [&logger, &target_field_name](auto const& msg) {
                (*logger)(
                    fmt::format("While evaluating target parameter {}:\n{}",
                                target_field_name,
                                msg),
                    true);
            });
        if (not deps_names) {
            return;
        }
        if (not deps_names->IsList()) {
            (*logger)(fmt::format("Target parameter {} should evaluate to a "
                                  "list, but got {}",
                                  target_field_name,
                                  deps_names->ToString()),
                      true);
            return;
        }
        Expression::list_t dep_target_exps;
        if (data->parse_target_names) {
            dep_target_exps.reserve(deps_names->List().size());
            for (const auto& dep_name : deps_names->List()) {
                auto target = BuildMaps::Base::ParseEntityNameFromExpression(
                    dep_name,
                    key.target,
                    context->repo_config,
                    [&logger, &target_field_name, &dep_name](
                        std::string const& parse_err) {
                        (*logger)(fmt::format("Parsing entry {} in target "
                                              "field {} failed with:\n{}",
                                              dep_name->ToString(),
                                              target_field_name,
                                              parse_err),
                                  true);
                    });
                if (not target) {
                    return;
                }
                dep_target_exps.emplace_back(*target);
            }
        }
        else {
            dep_target_exps = deps_names->List();
        }
        auto anon_pos = anon_positions.find(target_field_name);
        auto const& transitions = config_transitions[target_field_name]->List();
        for (const auto& transition : transitions) {
            auto transitioned_config = key.config.Update(transition);
            for (const auto& dep : dep_target_exps) {
                if (anon_pos != anon_positions.end()) {
                    anon_pos->second.emplace_back(dependency_keys.size());
                }

                dependency_keys.emplace_back(
                    BuildMaps::Target::ConfiguredTarget{
                        .target = dep->Name(), .config = transitioned_config});
                transition_keys.emplace_back(
                    BuildMaps::Target::ConfiguredTarget{
                        .target = dep->Name(),
                        .config = Configuration{transition}});
            }
        }
        params.emplace(target_field_name,
                       ExpressionPtr{std::move(dep_target_exps)});
    }
    auto declared_count = dependency_keys.size();
    for (auto const& [implicit_field_name, implicit_target] :
         rule->ImplicitTargets()) {
        auto anon_pos = anon_positions.find(implicit_field_name);
        auto transitions = config_transitions[implicit_field_name]->List();
        for (const auto& transition : transitions) {
            auto transitioned_config = key.config.Update(transition);
            for (const auto& dep : implicit_target) {
                if (anon_pos != anon_positions.end()) {
                    anon_pos->second.emplace_back(dependency_keys.size());
                }

                dependency_keys.emplace_back(
                    BuildMaps::Target::ConfiguredTarget{
                        .target = dep, .config = transitioned_config});
                transition_keys.emplace_back(
                    BuildMaps::Target::ConfiguredTarget{
                        .target = dep, .config = Configuration{transition}});
            }
        }
    }
    params.insert(rule->ImplicitTargetExps().begin(),
                  rule->ImplicitTargetExps().end());
    auto declared_and_implicit_count = dependency_keys.size();

    (*subcaller)(
        dependency_keys,
        [context,
         transition_keys = std::move(transition_keys),
         declared_count,
         declared_and_implicit_count,
         rule,
         data,
         key,
         params = std::move(params),
         setter,
         logger,
         result_map,
         subcaller,
         config_transitions = std::move(config_transitions),
         anon_positions =
             std::move(anon_positions)](auto const& values) mutable {
            // Now that all non-anonymous targets have been evaluated we can
            // read their provides map to construct and evaluate anonymous
            // targets.
            std::vector<BuildMaps::Target::ConfiguredTarget> anonymous_keys;
            for (auto const& [name, def] : rule->AnonymousDefinitions()) {
                Expression::list_t anon_names{};
                for (auto pos : anon_positions.at(def.target)) {
                    auto const& provider_value =
                        (*values[pos])->Provides()->Map().Find(def.provider);
                    if (not provider_value) {
                        (*logger)(
                            fmt::format("Provider {} in {} does not exist",
                                        def.provider,
                                        def.target),
                            true);
                        return;
                    }
                    auto const& exprs = **provider_value;
                    if (not exprs->IsList()) {
                        (*logger)(fmt::format("Provider {} in {} must be list "
                                              "of target nodes but found: {}",
                                              def.provider,
                                              def.target,
                                              exprs->ToString()),
                                  true);
                        return;
                    }

                    auto const& list = exprs->List();
                    anon_names.reserve(anon_names.size() + list.size());
                    for (auto const& node : list) {
                        if (not node->IsNode()) {
                            (*logger)(
                                fmt::format("Entry in provider {} in {} must "
                                            "be target node but found: {}",
                                            def.provider,
                                            def.target,
                                            node->ToString()),
                                true);
                            return;
                        }
                        anon_names.emplace_back(BuildMaps::Base::EntityName{
                            BuildMaps::Base::AnonymousTarget{
                                .rule_map = def.rule_map,
                                .target_node = node}});
                    }
                }

                for (const auto& transition :
                     config_transitions.at(name)->List()) {
                    auto transitioned_config = key.config.Update(transition);
                    for (auto const& anon : anon_names) {
                        anonymous_keys.emplace_back(
                            BuildMaps::Target::ConfiguredTarget{
                                .target = anon->Name(),
                                .config = transitioned_config});

                        transition_keys.emplace_back(
                            BuildMaps::Target::ConfiguredTarget{
                                .target = anon->Name(),
                                .config = Configuration{transition}});
                    }
                }

                params.emplace(name, ExpressionPtr{std::move(anon_names)});
            }
            (*subcaller)(
                anonymous_keys,
                [context,
                 dependency_values = values,
                 transition_keys = std::move(transition_keys),
                 declared_count,
                 declared_and_implicit_count,
                 rule,
                 data,
                 key,
                 params = std::move(params),
                 setter,
                 logger,
                 result_map](auto const& values) mutable {
                    // Join dependency values and anonymous values
                    dependency_values.insert(
                        dependency_values.end(), values.begin(), values.end());
                    withDependencies(context,
                                     transition_keys,
                                     dependency_values,
                                     declared_count,
                                     declared_and_implicit_count,
                                     rule,
                                     data,
                                     key,
                                     params,
                                     setter,
                                     logger,
                                     result_map);
                },
                logger);
        },
        logger);
}

void withTargetsFile(
    const gsl::not_null<AnalyseContext*>& context,
    const BuildMaps::Target::ConfiguredTarget& key,
    const nlohmann::json& targets_file,
    const gsl::not_null<BuildMaps::Base::SourceTargetMap*>& source_target,
    const gsl::not_null<BuildMaps::Base::UserRuleMap*>& rule_map,
    const gsl::not_null<TaskSystem*>& ts,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*> result_map) {
    auto desc_it = targets_file.find(key.target.GetNamedTarget().name);
    if (desc_it == targets_file.end()) {
        // Not a defined taraget, treat as source target
        source_target->ConsumeAfterKeysReady(
            ts,
            {key.target},
            [setter](auto values) { (*setter)(AnalysedTargetPtr{*values[0]}); },
            [logger, target = key.target](auto const& msg, auto fatal) {
                (*logger)(fmt::format("While analysing target {} as implicit "
                                      "source target:\n{}",
                                      target.ToString(),
                                      msg),
                          fatal);
            });
    }
    else {
        nlohmann::json desc = *desc_it;
        auto rule_it = desc.find("type");
        if (rule_it == desc.end()) {
            (*logger)(
                fmt::format("No type specified in the definition of target {}",
                            key.target.ToString()),
                true);
            return;
        }
        // Handle built-in rule, if it is
        auto handled_as_builtin = BuildMaps::Target::HandleBuiltin(context,
                                                                   *rule_it,
                                                                   desc,
                                                                   key,
                                                                   subcaller,
                                                                   setter,
                                                                   logger,
                                                                   result_map);
        if (handled_as_builtin) {
            return;
        }

        // Not a built-in rule, so has to be a user rule
        auto rule_name = BuildMaps::Base::ParseEntityNameFromJson(
            *rule_it,
            key.target,
            context->repo_config,
            [&logger, &rule_it, &key](std::string const& parse_err) {
                (*logger)(fmt::format("Parsing rule name {} for target {} "
                                      "failed with:\n{}",
                                      rule_it->dump(),
                                      key.target.ToString(),
                                      parse_err),
                          true);
            });
        if (not rule_name) {
            return;
        }
        auto desc_reader = BuildMaps::Base::FieldReader::CreatePtr(
            desc,
            key.target,
            fmt::format("{} target", rule_name->ToString()),
            logger);
        if (not desc_reader) {
            return;
        }
        rule_map->ConsumeAfterKeysReady(
            ts,
            {*rule_name},
            [desc = std::move(desc_reader),
             subcaller,
             setter,
             logger,
             key,
             context,
             result_map,
             rn = *rule_name](auto values) {
                auto data = TargetData::FromFieldReader(*values[0], desc);
                if (not data) {
                    (*logger)(fmt::format("Failed to read data from target {} "
                                          "with rule {}",
                                          key.target.ToString(),
                                          rn.ToString()),
                              /*fatal=*/true);
                    return;
                }
                withRuleDefinition(
                    context,
                    *values[0],
                    data,
                    key,
                    subcaller,
                    setter,
                    std::make_shared<AsyncMapConsumerLogger>(
                        [logger, key, rn](auto const& msg, auto fatal) {
                            (*logger)(
                                fmt::format(
                                    "While analysing {} target {}:\n{}",
                                    rn.ToString(),
                                    key.ToShortString(
                                        Evaluator::GetExpressionLogLimit()),
                                    msg),
                                fatal);
                        }),
                    result_map);
            },
            [logger, rule = *rule_name, target = key.target](auto const& msg,
                                                             auto fatal) {
                (*logger)(fmt::format("While looking up rule {} for {}:\n{}",
                                      rule.ToString(),
                                      target.ToString(),
                                      msg),
                          fatal);
            });
    }
}

void withTargetNode(
    const gsl::not_null<AnalyseContext*>& context,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<BuildMaps::Base::UserRuleMap*>& rule_map,
    const gsl::not_null<TaskSystem*>& ts,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*> result_map) {
    auto const& target_node =
        key.target.GetAnonymousTarget().target_node->Node();
    auto const& rule_mapping = key.target.GetAnonymousTarget().rule_map->Map();
    if (target_node.IsValue()) {
        // fixed value node, create analysed target from result
        auto const& val = target_node.GetValue();
        (*setter)(std::make_shared<AnalysedTarget const>(
            AnalysedTarget{val->Result(),
                           {},
                           {},
                           {},
                           {},
                           {},
                           {},
                           TargetGraphInformation::kSource}));
    }
    else {
        // abstract target node, lookup rule and instantiate target
        auto const& abs = target_node.GetAbstract();
        auto rule_name = rule_mapping.Find(abs.node_type);
        if (not rule_name) {
            (*logger)(fmt::format(
                          "Cannot resolve type of node {} via rule map "
                          "{}",
                          target_node.ToString(),
                          key.target.GetAnonymousTarget().rule_map->ToString()),
                      /*fatal=*/true);
            return;
        }
        rule_map->ConsumeAfterKeysReady(
            ts,
            {(**rule_name)->Name()},
            [abs,
             subcaller,
             setter,
             logger,
             key,
             context,
             result_map,
             rn = **rule_name](auto values) {
                auto data = TargetData::FromTargetNode(
                    *values[0],
                    abs,
                    key.target.GetAnonymousTarget().rule_map,
                    logger);
                if (not data) {
                    (*logger)(fmt::format("Failed to read data from target {} "
                                          "with rule {}",
                                          key.target.ToString(),
                                          rn->ToString()),
                              /*fatal=*/true);
                    return;
                }
                withRuleDefinition(context,
                                   *values[0],
                                   data,
                                   key,
                                   subcaller,
                                   setter,
                                   std::make_shared<AsyncMapConsumerLogger>(
                                       [logger, target = key.target, rn](
                                           auto const& msg, auto fatal) {
                                           (*logger)(
                                               fmt::format("While analysing {} "
                                                           "target {}:\n{}",
                                                           rn->ToString(),
                                                           target.ToString(),
                                                           msg),
                                               fatal);
                                       }),
                                   result_map);
            },
            [logger, target = key.target](auto const& msg, auto fatal) {
                (*logger)(fmt::format("While looking up rule for {}:\n{}",
                                      target.ToString(),
                                      msg),
                          fatal);
            });
    }
}

void TreeTarget(
    const gsl::not_null<AnalyseContext*>& context,
    const BuildMaps::Target::ConfiguredTarget& key,
    const gsl::not_null<TaskSystem*>& ts,
    const BuildMaps::Target::TargetMap::SubCallerPtr& subcaller,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Target::ResultTargetMap*>& result_map,
    const gsl::not_null<BuildMaps::Base::DirectoryEntriesMap*>&
        directory_entries) {
    const auto& target = key.target.GetNamedTarget();
    const auto dir_name = std::filesystem::path{target.module} / target.name;
    auto target_module =
        BuildMaps::Base::ModuleName{target.repository, dir_name};

    directory_entries->ConsumeAfterKeysReady(
        ts,
        {target_module},
        [context, setter, subcaller, target, key, result_map, logger, dir_name](
            auto values) {
            // expected values.size() == 1
            const auto& dir_entries = *values[0];
            auto known_tree = dir_entries.AsKnownTree(
                context->storage->GetHashFunction().GetType(),
                target.repository);
            if (known_tree) {
                auto tree = ExpressionPtr{
                    Expression::map_t{target.name, ExpressionPtr{*known_tree}}};

                auto analysis_result = std::make_shared<AnalysedTarget const>(
                    TargetResult{.artifact_stage = tree,
                                 .provides = {},
                                 .runfiles = tree},
                    std::vector<ActionDescription::Ptr>{},
                    std::vector<std::string>{},
                    std::vector<Tree::Ptr>{},
                    std::unordered_set<std::string>{},
                    std::set<std::string>{},
                    std::set<std::string>{},
                    TargetGraphInformation::kSource);
                analysis_result =
                    result_map->Add(key.target, {}, std::move(analysis_result));
                (*setter)(std::move(analysis_result));
                return;
            }
            Logger::Log(LogLevel::Debug, [&key]() {
                return fmt::format(
                    "Source tree reference for non-known tree {}",
                    key.target.ToString());
            });
            context->statistics->IncrementTreesAnalysedCounter();

            using BuildMaps::Target::ConfiguredTarget;

            std::vector<ConfiguredTarget> v;

            for (const auto& x : dir_entries.FilesIterator()) {
                v.emplace_back(ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            target.repository,
                            dir_name,
                            x,
                            BuildMaps::Base::ReferenceType::kFile},
                    .config = Configuration{}});
            }

            for (const auto& x : dir_entries.SymlinksIterator()) {
                v.emplace_back(ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            target.repository,
                            dir_name,
                            x,
                            BuildMaps::Base::ReferenceType::kSymlink},
                    .config = Configuration{}});
            }

            for (const auto& x : dir_entries.DirectoriesIterator()) {
                v.emplace_back(ConfiguredTarget{
                    .target =
                        BuildMaps::Base::EntityName{
                            target.repository,
                            dir_name,
                            x,
                            BuildMaps::Base::ReferenceType::kTree},
                    .config = Configuration{}});
            }
            (*subcaller)(
                std::move(v),
                [setter, key, result_map, name = target.name](
                    const auto& values) mutable {
                    std::unordered_map<std::string, ArtifactDescription>
                        artifacts;

                    artifacts.reserve(values.size());

                    for (const auto& x : values) {
                        auto val = x->get()->RunFiles();

                        auto const& [input_path, artifact] =
                            *(val->Map().begin());
                        auto norm_path =
                            ToNormalPath(std::filesystem::path{input_path})
                                .string();

                        artifacts.emplace(std::move(norm_path),
                                          artifact->Artifact());
                    }

                    auto tree = std::make_shared<Tree>(std::move(artifacts));
                    auto tree_id = tree->Id();
                    auto tree_map = ExpressionPtr{Expression::map_t{
                        name,
                        ExpressionPtr{
                            ArtifactDescription::CreateTree(tree_id)}}};
                    auto analysis_result =
                        std::make_shared<AnalysedTarget const>(
                            TargetResult{.artifact_stage = tree_map,
                                         .provides = {},
                                         .runfiles = tree_map},
                            std::vector<ActionDescription::Ptr>{},
                            std::vector<std::string>{},
                            std::vector<Tree::Ptr>{tree},
                            std::unordered_set<std::string>{},
                            std::set<std::string>{},
                            std::set<std::string>{},
                            TargetGraphInformation::kSource);
                    analysis_result = result_map->Add(
                        key.target, {}, std::move(analysis_result));
                    (*setter)(std::move(analysis_result));
                },
                logger);
        },
        [logger, target = key.target](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While analysing entries of {}: {}",
                                  target.ToString(),
                                  msg),
                      fatal);
        });
}

void GlobResult(const std::vector<AnalysedTargetPtr const*>& values,
                const BuildMaps::Target::TargetMap::SetterPtr& setter) {
    auto result = Expression::map_t::underlying_map_t{};
    for (auto const& value : values) {
        for (auto const& [k, v] : (*value)->Artifacts()->Map()) {
            result[k] = v;
        }
    }
    auto stage = ExpressionPtr{Expression::map_t{result}};
    auto target = std::make_shared<AnalysedTarget const>(
        TargetResult{.artifact_stage = stage,
                     .provides = Expression::kEmptyMap,
                     .runfiles = stage},
        std::vector<ActionDescription::Ptr>{},
        std::vector<std::string>{},
        std::vector<Tree::Ptr>{},
        std::unordered_set<std::string>{},
        std::set<std::string>{},
        std::set<std::string>{},
        TargetGraphInformation::kSource);
    (*setter)(std::move(target));
}

void GlobTargetWithDirEntry(
    const BuildMaps::Base::EntityName& key,
    const gsl::not_null<TaskSystem*>& ts,
    const BuildMaps::Target::TargetMap::SetterPtr& setter,
    const BuildMaps::Target::TargetMap::LoggerPtr& logger,
    const gsl::not_null<BuildMaps::Base::SourceTargetMap*>& source_target_map,
    const FileRoot::DirectoryEntries& dir) {
    auto const& target = key.GetNamedTarget();
    auto const& pattern = target.name;
    std::vector<BuildMaps::Base::EntityName> matches;
    for (auto const& x : dir.FilesIterator()) {
        if (fnmatch(pattern.c_str(), x.c_str(), 0) == 0) {
            matches.emplace_back(target.repository,
                                 target.module,
                                 x,
                                 BuildMaps::Base::ReferenceType::kFile);
        }
    }
    for (auto const& x : dir.SymlinksIterator()) {
        if (fnmatch(pattern.c_str(), x.c_str(), 0) == 0) {
            matches.emplace_back(target.repository,
                                 target.module,
                                 x,
                                 BuildMaps::Base::ReferenceType::kSymlink);
        }
    }
    source_target_map->ConsumeAfterKeysReady(
        ts,
        matches,
        [setter](auto values) { GlobResult(values, setter); },
        [logger](auto const& msg, bool fatal) {
            (*logger)(
                fmt::format("While handling matching file targets:\n{}", msg),
                fatal);
        });
}

}  // namespace

namespace BuildMaps::Target {
auto CreateTargetMap(
    const gsl::not_null<AnalyseContext*>& context,
    const gsl::not_null<BuildMaps::Base::SourceTargetMap*>& source_target_map,
    const gsl::not_null<BuildMaps::Base::TargetsFileMap*>& targets_file_map,
    const gsl::not_null<BuildMaps::Base::UserRuleMap*>& rule_map,
    const gsl::not_null<BuildMaps::Base::DirectoryEntriesMap*>&
        directory_entries_map,
    const gsl::not_null<AbsentTargetMap*>& absent_target_map,
    const gsl::not_null<ResultTargetMap*>& result_map,
    std::size_t jobs) -> TargetMap {
    auto target_reader = [source_target_map,
                          targets_file_map,
                          rule_map,
                          directory_entries_map,
                          absent_target_map,
                          result_map,
                          context](auto ts,
                                   auto setter,
                                   auto logger,
                                   auto subcaller,
                                   auto key) {
        if (key.target.IsAnonymousTarget()) {
            withTargetNode(context,
                           key,
                           rule_map,
                           ts,
                           subcaller,
                           setter,
                           logger,
                           result_map);
        }
        else if (key.target.GetNamedTarget().reference_t ==
                 BuildMaps::Base::ReferenceType::kTree) {

            auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [logger, target = key.target](auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While analysing {} as explicit "
                                          "tree reference:\n{}",
                                          target.ToString(),
                                          msg),
                              fatal);
                });
            TreeTarget(context,
                       key,
                       ts,
                       subcaller,
                       setter,
                       wrapped_logger,
                       result_map,
                       directory_entries_map);
        }
        else if (key.target.GetNamedTarget().reference_t ==
                 BuildMaps::Base::ReferenceType::kFile) {
            // Not a defined target, treat as source target
            source_target_map->ConsumeAfterKeysReady(
                ts,
                {key.target},
                [setter](auto values) {
                    (*setter)(AnalysedTargetPtr{*values[0]});
                },
                [logger, target = key.target](auto const& msg, auto fatal) {
                    (*logger)(fmt::format("While analysing target {} as "
                                          "explicit source target:\n{}",
                                          target.ToString(),
                                          msg),
                              fatal);
                });
        }
        else if (key.target.GetNamedTarget().reference_t ==
                 BuildMaps::Base::ReferenceType::kSymlink) {
            // Not a defined target, treat as source target
            source_target_map->ConsumeAfterKeysReady(
                ts,
                {key.target},
                [setter](auto values) {
                    (*setter)(AnalysedTargetPtr{*values[0]});
                },
                [logger, target = key.target](auto const& msg, auto fatal) {
                    (*logger)(fmt::format("While analysing target {} as "
                                          "symlink:\n{}",
                                          target.ToString(),
                                          msg),
                              fatal);
                });
        }
        else if (key.target.GetNamedTarget().reference_t ==
                 BuildMaps::Base::ReferenceType::kGlob) {
            auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [logger, target = key.target](auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While analysing {} as glob:\n{}",
                                          target.ToString(),
                                          msg),
                              fatal);
                });
            auto const& target = key.target;
            directory_entries_map->ConsumeAfterKeysReady(
                ts,
                {target.ToModule()},
                [target, ts, setter, wrapped_logger, source_target_map](
                    auto values) {
                    GlobTargetWithDirEntry(target,
                                           ts,
                                           setter,
                                           wrapped_logger,
                                           source_target_map,
                                           *values[0]);
                },
                [target, logger](auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While reading directory for {}:\n{}",
                                          target.ToString(),
                                          msg),
                              fatal);
                }

            );
        }
#ifndef BOOTSTRAP_BUILD_TOOL
        else if (auto const* const file_root = context->repo_config->TargetRoot(
                     key.target.ToModule().repository);
                 file_root != nullptr and file_root->IsAbsent()) {
            if (context->serve == nullptr) {
                (*logger)(
                    fmt::format("Root for target {} is absent, but no serve "
                                "endpoint was configured. Please provide "
                                "--remote-serve-address and retry.",
                                key.target.ToJson().dump()),
                    /*is_fatal=*/true);
                return;
            }
            if (not context->serve->CheckServeRemoteExecution()) {
                (*logger)(
                    "Inconsistent remote execution endpoint and serve endpoint"
                    "configuration detected.",
                    /*is_fatal=*/true);
                return;
            }
            absent_target_map->ConsumeAfterKeysReady(
                ts,
                {key},
                [setter](auto values) {
                    (*setter)(AnalysedTargetPtr{*values[0]});
                },
                [logger, key](auto msg, auto fatal) {
                    (*logger)(
                        fmt::format("While processing absent target {}:\n{}",
                                    key.ToShortString(
                                        Evaluator::GetExpressionLogLimit()),
                                    msg),
                        fatal);
                });
        }
#endif
        else {
            targets_file_map->ConsumeAfterKeysReady(
                ts,
                {key.target.ToModule()},
                [key,
                 context,
                 source_target_map,
                 rule_map,
                 ts,
                 subcaller = std::move(subcaller),
                 setter = std::move(setter),
                 logger,
                 result_map](auto values) {
                    withTargetsFile(context,
                                    key,
                                    *values[0],
                                    source_target_map,
                                    rule_map,
                                    ts,
                                    subcaller,
                                    setter,
                                    logger,
                                    result_map);
                },
                [logger, target = key.target](auto const& msg, auto fatal) {
                    (*logger)(fmt::format("While searching targets "
                                          "description for {}:\n{}",
                                          target.ToString(),
                                          msg),
                              fatal);
                });
        }
    };
    return AsyncMapConsumer<ConfiguredTarget, AnalysedTargetPtr>(target_reader,
                                                                 jobs);
}
}  // namespace BuildMaps::Target
