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

#include "src/buildtool/build_engine/base_maps/rule_map.hpp"

#include <optional>
#include <string>
#include <unordered_set>

#include "fmt/core.h"
#include "src/buildtool/build_engine/base_maps/field_reader.hpp"

namespace BuildMaps::Base {

namespace {

auto const kRuleFields = std::unordered_set<std::string>{"anonymous",
                                                         "artifacts_doc",
                                                         "config_doc",
                                                         "config_fields",
                                                         "config_transitions",
                                                         "config_vars",
                                                         "doc",
                                                         "expression",
                                                         "field_doc",
                                                         "implicit",
                                                         "imports",
                                                         "provides_doc",
                                                         "runfiles_doc",
                                                         "string_fields",
                                                         "tainted",
                                                         "target_fields"};

[[nodiscard]] auto ReadAnonymousObject(
    EntityName const& id,
    nlohmann::json const& json,
    gsl::not_null<RepositoryConfig*> const& repo_config,
    AsyncMapConsumerLoggerPtr const& logger)
    -> std::optional<UserRule::anonymous_defs_t> {
    auto obj = GetOrDefault(json, "anonymous", nlohmann::json::object());
    if (not obj.is_object()) {
        (*logger)(fmt::format("Field anonymous in rule {} is not an object",
                              id.GetNamedTarget().name),
                  true);
        return std::nullopt;
    }

    UserRule::anonymous_defs_t anon_defs{};
    anon_defs.reserve(obj.size());
    for (auto const& [name, def] : obj.items()) {
        if (not def.is_object()) {
            (*logger)(fmt::format("Entry {} in field anonymous in rule {} is "
                                  "not an object",
                                  name,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }

        auto target = def.find("target");
        if (target == def.end()) {
            (*logger)(fmt::format("Entry target for {} in field anonymous in "
                                  "rule {} is missing",
                                  name,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }
        if (not target->is_string()) {
            (*logger)(fmt::format("Entry target for {} in field anonymous in "
                                  "rule {} is not a string",
                                  name,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }

        auto provider = def.find("provider");
        if (provider == def.end()) {
            (*logger)(fmt::format("Entry provider for {} in field anonymous in "
                                  "rule {} is missing",
                                  name,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }
        if (not provider->is_string()) {
            (*logger)(fmt::format("Entry provider for {} in field anonymous in "
                                  "rule {} is not a string",
                                  name,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }

        auto rule_map = def.find("rule_map");
        if (rule_map == def.end()) {
            (*logger)(fmt::format("Entry rule_map for {} in field anonymous in "
                                  "rule {} is missing",
                                  name,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }
        if (not rule_map->is_object()) {
            (*logger)(fmt::format("Entry rule_map for {} in field anonymous in "
                                  "rule {} is not an object",
                                  name,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }

        Expression::map_t::underlying_map_t rule_mapping{};
        for (auto const& [key, val] : rule_map->items()) {
            auto rule_name = ParseEntityNameFromJson(
                val, id, repo_config, [&logger, &id, &name = name](auto msg) {
                    (*logger)(
                        fmt::format("Parsing rule name for entry {} in field "
                                    "anonymous in rule {} failed with:\n{}",
                                    name,
                                    id.GetNamedTarget().name,
                                    msg),
                        true);
                });
            if (not rule_name) {
                return std::nullopt;
            }
            rule_mapping.emplace(key, ExpressionPtr{std::move(*rule_name)});
        }

        anon_defs.emplace(name,
                          UserRule::AnonymousDefinition{
                              .target = target->get<std::string>(),
                              .provider = provider->get<std::string>(),
                              .rule_map = ExpressionPtr{
                                  Expression::map_t{std::move(rule_mapping)}}});
    }
    return anon_defs;
}

[[nodiscard]] auto ReadImplicitObject(
    EntityName const& id,
    nlohmann::json const& json,
    gsl::not_null<RepositoryConfig*> const& repo_config,
    AsyncMapConsumerLoggerPtr const& logger)
    -> std::optional<UserRule::implicit_t> {
    auto map = GetOrDefault(json, "implicit", nlohmann::json::object());
    if (not map.is_object()) {
        (*logger)(fmt::format("Field implicit in rule {} is not an object",
                              id.GetNamedTarget().name),
                  true);
        return std::nullopt;
    }

    auto implicit_targets = UserRule::implicit_t{};
    implicit_targets.reserve(map.size());

    for (auto const& [key, val] : map.items()) {
        if (not val.is_array()) {
            (*logger)(fmt::format("Entry in implicit field of rule {} is not a "
                                  "list.",
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }
        auto targets = typename UserRule::implicit_t::mapped_type{};
        targets.reserve(val.size());
        for (auto const& item : val) {
            auto expr_id = ParseEntityNameFromJson(
                item,
                id,
                repo_config,
                [&logger, &item, &id](std::string const& parse_err) {
                    (*logger)(fmt::format("Parsing entry {} in implicit field "
                                          "of rule {} failed with:\n{}",
                                          item.dump(),
                                          id.GetNamedTarget().name,
                                          parse_err),
                              true);
                });
            if (not expr_id) {
                return std::nullopt;
            }
            targets.emplace_back(*expr_id);
        }
        implicit_targets.emplace(key, targets);
    }
    return implicit_targets;
}

[[nodiscard]] auto ReadConfigTransitionsObject(
    EntityName const& id,
    nlohmann::json const& json,
    std::vector<std::string> const& config_vars,
    ExpressionFunction::imports_t const& imports,
    AsyncMapConsumerLoggerPtr const& logger)
    -> std::optional<UserRule::config_trans_t> {
    auto map =
        GetOrDefault(json, "config_transitions", nlohmann::json::object());
    if (not map.is_object()) {
        (*logger)(
            fmt::format("Field config_transitions in rule {} is not an object",
                        id.GetNamedTarget().name),
            true);
        return std::nullopt;
    }

    auto config_transitions = UserRule::config_trans_t{};
    config_transitions.reserve(map.size());

    for (auto const& [key, val] : map.items()) {
        auto expr = Expression::FromJson(val);
        if (not expr) {
            (*logger)(fmt::format("Failed to create expression for entry {} in "
                                  "config_transitions list of rule {}.",
                                  key,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }
        config_transitions.emplace(
            key,
            std::make_shared<ExpressionFunction>(config_vars, imports, expr));
    }
    return config_transitions;
}

}  // namespace

auto CreateRuleMap(gsl::not_null<RuleFileMap*> const& rule_file_map,
                   gsl::not_null<ExpressionFunctionMap*> const& expr_map,
                   gsl::not_null<RepositoryConfig*> const& repo_config,
                   std::size_t jobs) -> UserRuleMap {
    auto user_rule_creator = [rule_file_map, expr_map, repo_config](
                                 auto ts,
                                 auto setter,
                                 auto logger,
                                 auto /*subcaller*/,
                                 auto const& id) {
        if (not id.IsDefinitionName()) {
            (*logger)(fmt::format("{} cannot name a rule", id.ToString()),
                      true);
            return;
        }
        rule_file_map->ConsumeAfterKeysReady(
            ts,
            {id.ToModule()},
            [ts, expr_map, repo_config, setter = std::move(setter), logger, id](
                auto json_values) {
                const auto& target_ = id.GetNamedTarget();
                auto rule_it = json_values[0]->find(target_.name);
                if (rule_it == json_values[0]->end()) {
                    (*logger)(
                        fmt::format("Cannot find rule {} in {}",
                                    nlohmann::json(target_.name).dump(),
                                    nlohmann::json(target_.module).dump()),
                        true);
                    return;
                }

                auto reader =
                    FieldReader::Create(rule_it.value(), id, "rule", logger);
                if (not reader) {
                    return;
                }
                reader->ExpectFields(kRuleFields);

                auto expr = reader->ReadExpression("expression");
                if (not expr) {
                    return;
                }

                auto target_fields = reader->ReadStringList("target_fields");
                if (not target_fields) {
                    return;
                }

                auto string_fields = reader->ReadStringList("string_fields");
                if (not string_fields) {
                    return;
                }

                auto config_fields = reader->ReadStringList("config_fields");
                if (not config_fields) {
                    return;
                }

                auto implicit_targets = ReadImplicitObject(
                    id, rule_it.value(), repo_config, logger);
                if (not implicit_targets) {
                    return;
                }

                auto anonymous_defs = ReadAnonymousObject(
                    id, rule_it.value(), repo_config, logger);
                if (not anonymous_defs) {
                    return;
                }

                auto config_vars = reader->ReadStringList("config_vars");
                if (not config_vars) {
                    return;
                }

                auto tainted = reader->ReadStringList("tainted");
                if (not tainted) {
                    return;
                }

                auto import_aliases =
                    reader->ReadEntityAliasesObject("imports", repo_config);
                if (not import_aliases) {
                    return;
                }
                auto [names, ids] = std::move(*import_aliases).Obtain();

                expr_map->ConsumeAfterKeysReady(
                    ts,
                    std::move(ids),
                    [id,
                     json = rule_it.value(),
                     expr = std::move(expr),
                     target_fields = std::move(*target_fields),
                     string_fields = std::move(*string_fields),
                     config_fields = std::move(*config_fields),
                     implicit_targets = std::move(*implicit_targets),
                     anonymous_defs = std::move(*anonymous_defs),
                     config_vars = std::move(*config_vars),
                     tainted = std::move(*tainted),
                     names = std::move(names),
                     setter = std::move(setter),
                     logger](auto expr_funcs) {
                        auto imports = ExpressionFunction::imports_t{};
                        imports.reserve(expr_funcs.size());
                        for (std::size_t i{}; i < expr_funcs.size(); ++i) {
                            imports.emplace(names[i], *expr_funcs[i]);
                        }

                        auto config_transitions = ReadConfigTransitionsObject(
                            id, json, config_vars, imports, logger);
                        if (not config_transitions) {
                            return;
                        }

                        auto rule = UserRule::Create(
                            target_fields,
                            string_fields,
                            config_fields,
                            implicit_targets,
                            anonymous_defs,
                            config_vars,
                            tainted,
                            std::move(*config_transitions),
                            std::make_shared<ExpressionFunction>(
                                std::move(config_vars),
                                std::move(imports),
                                std::move(expr)),
                            [&logger](auto const& msg) {
                                (*logger)(msg, true);
                            });
                        if (rule) {
                            (*setter)(std::move(rule));
                        }
                    },
                    [logger, id](auto msg, auto fatal) {
                        (*logger)(fmt::format("While reading expression map "
                                              "for rule {}:\n{}",
                                              id.GetNamedTarget().ToString(),
                                              msg),
                                  fatal);
                    });
            },
            [logger, id](auto msg, auto fatal) {
                (*logger)(fmt::format("While reading rule file for {}:\n{}",
                                      id.GetNamedTarget().ToString(),
                                      msg),
                          fatal);
            });
    };
    return UserRuleMap{user_rule_creator, jobs};
}

}  // namespace BuildMaps::Base
