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

#include "src/buildtool/main/describe.hpp"

#include <iostream>
#include <optional>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/exit_codes.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/execution_api/common/create_execution_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#endif  // BOOTSTRAP_BUILD_TOOL

namespace {

namespace Base = BuildMaps::Base;

void PrintDoc(const nlohmann::json& doc, const std::string& indent) {
    if (not doc.is_array()) {
        return;
    }
    for (auto const& line : doc) {
        if (line.is_string()) {
            std::cout << indent << line.get<std::string>() << "\n";
        }
    }
}

void PrintFields(nlohmann::json const& fields,
                 const nlohmann::json& fdoc,
                 const std::string& indent_field,
                 const std::string& indent_field_doc) {
    for (auto const& f : fields) {
        std::cout << indent_field << f << "\n";
        auto doc = fdoc.find(f);
        if (doc != fdoc.end()) {
            PrintDoc(*doc, indent_field_doc);
        }
    }
}

void PrettyPrintRule(
    nlohmann::json const& rdesc,
    BuildMaps::Base::EntityName const& rule_name,
    gsl::not_null<const RepositoryConfig*> const& repo_config) {
    auto doc = rdesc.find("doc");
    if (doc != rdesc.end()) {
        PrintDoc(*doc, " | ");
    }
    auto field_doc = nlohmann::json::object();
    auto field_doc_it = rdesc.find("field_doc");
    if (field_doc_it != rdesc.end() and field_doc_it->is_object()) {
        field_doc = *field_doc_it;
    }
    auto string_fields = rdesc.find("string_fields");
    if (string_fields != rdesc.end() and (not string_fields->empty())) {
        std::cout << " String fields\n";
        PrintFields(*string_fields, field_doc, " - ", "   | ");
    }
    auto target_fields = rdesc.find("target_fields");
    if (target_fields != rdesc.end() and (not target_fields->empty())) {
        std::cout << " Target fields\n";
        PrintFields(*target_fields, field_doc, " - ", "   | ");
    }
    auto implicit_targets = rdesc.find("implicit");
    if (implicit_targets != rdesc.end()) {
        for (auto const& [key, value] : implicit_targets->items()) {
            std::cout << " - implict dependency\n";  //
            auto doc = field_doc.find(key);
            if (doc != field_doc.end()) {
                PrintDoc(*doc, "   | ");
            }
            for (auto const& entry : value) {
                auto resolved_entry = BuildMaps::Base::ParseEntityNameFromJson(
                    entry,
                    rule_name,
                    repo_config,
                    [&entry, &rule_name](std::string const& parse_err) {
                        Logger::Log(LogLevel::Warning,
                                    "Failed to resolve {} relative to {}:\n{}",
                                    entry.dump(),
                                    rule_name.ToString(),
                                    parse_err);
                    });
                if (resolved_entry) {
                    std::cout << "   - " << resolved_entry->ToString() << "\n";
                }
            }
        }
    }
    auto config_fields = rdesc.find("config_fields");
    if (config_fields != rdesc.end() and (not config_fields->empty())) {
        std::cout << " Config fields\n";
        PrintFields(*config_fields, field_doc, " - ", "   | ");
    }
    auto config_doc = nlohmann::json::object();
    auto config_doc_it = rdesc.find("config_doc");
    if (config_doc_it != rdesc.end() and config_doc_it->is_object()) {
        config_doc = *config_doc_it;
    }
    auto config_vars = rdesc.find("config_vars");
    if (config_vars != rdesc.end() and (not config_vars->empty())) {
        std::cout << " Variables taken from the configuration\n";
        PrintFields(*config_vars, config_doc, " - ", "   | ");
    }
    std::cout << " Result\n";
    std::cout << " - Artifacts\n";
    auto artifacts_doc = rdesc.find("artifacts_doc");
    if (artifacts_doc != rdesc.end()) {
        PrintDoc(*artifacts_doc, "   | ");
    }
    std::cout << " - Runfiles\n";
    auto runfiles_doc = rdesc.find("runfiles_doc");
    if (runfiles_doc != rdesc.end()) {
        PrintDoc(*runfiles_doc, "   | ");
    }
    auto provides_doc = rdesc.find("provides_doc");
    if (provides_doc != rdesc.end()) {
        std::cout << " - Documented providers\n";
        for (auto const& el : provides_doc->items()) {
            std::cout << "   - " << el.key() << "\n";
            PrintDoc(el.value(), "     | ");
        }
    }
    std::cout << std::endl;
}

void PrintRuleAsOrderedJson(nlohmann::json const& rdesc,
                            nlohmann::json const& rule_name) {
    auto string_fields = nlohmann::json::array();
    if (auto doc = rdesc.find("string_fields"); doc != rdesc.end()) {
        string_fields = *doc;
    }
    auto target_fields = nlohmann::json::array();
    if (auto doc = rdesc.find("target_fields"); doc != rdesc.end()) {
        target_fields = *doc;
    }
    auto config_fields = nlohmann::json::array();
    if (auto doc = rdesc.find("config_fields"); doc != rdesc.end()) {
        config_fields = *doc;
    }
    auto config_vars = nlohmann::json::array();
    if (auto doc = rdesc.find("config_vars"); doc != rdesc.end()) {
        config_vars = *doc;
    }

    auto field_doc = nlohmann::ordered_json::object();
    if (auto doc = rdesc.find("field_doc");
        doc != rdesc.end() and doc->is_object()) {
        auto fields = nlohmann::json::array();
        fields.insert(fields.end(), string_fields.begin(), string_fields.end());
        fields.insert(fields.end(), target_fields.begin(), target_fields.end());
        fields.insert(fields.end(), config_fields.begin(), config_fields.end());
        for (auto const& field : fields) {
            if (doc->contains(field)) {
                auto name = field.get<std::string>();
                field_doc[name] = (*doc)[name];
            }
        }
    }

    auto config_doc = nlohmann::ordered_json::object();
    if (auto doc = rdesc.find("config_doc");
        doc != rdesc.end() and doc->is_object()) {
        for (auto const& var : config_vars) {
            if (doc->contains(var)) {
                auto name = var.get<std::string>();
                config_doc[name] = (*doc)[name];
            }
        }
    }

    auto json_doc = nlohmann::ordered_json::object();
    json_doc["type"] = rule_name;
    if (auto doc = rdesc.find("doc"); doc != rdesc.end()) {
        json_doc["doc"] = *doc;
    }
    if (not string_fields.empty()) {
        json_doc["string_fields"] = string_fields;
    }
    if (not target_fields.empty()) {
        json_doc["target_fields"] = target_fields;
    }
    if (not config_fields.empty()) {
        json_doc["config_fields"] = config_fields;
    }
    if (not field_doc.empty()) {
        json_doc["field_doc"] = field_doc;
    }
    if (not config_vars.empty()) {
        json_doc["config_vars"] = config_vars;
    }
    if (not config_doc.empty()) {
        json_doc["config_doc"] = config_doc;
    }
    if (auto doc = rdesc.find("artifacts_doc"); doc != rdesc.end()) {
        json_doc["artifacts_doc"] = *doc;
    }
    if (auto doc = rdesc.find("runfiles_doc"); doc != rdesc.end()) {
        json_doc["runfiles_doc"] = *doc;
    }
    if (auto doc = rdesc.find("provides_doc"); doc != rdesc.end()) {
        json_doc["provides_doc"] = *doc;
    }
    std::cout << json_doc.dump(2) << std::endl;
}

}  // namespace

auto DescribeUserDefinedRule(
    BuildMaps::Base::EntityName const& rule_name,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs,
    bool print_json) -> int {
    bool failed{};
    auto rule_file_map = Base::CreateRuleFileMap(repo_config, jobs);
    nlohmann::json rules_file;
    {
        TaskSystem ts{jobs};
        rule_file_map.ConsumeAfterKeysReady(
            &ts,
            {rule_name.ToModule()},
            [&rules_file](auto values) { rules_file = *values[0]; },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While searching for rule definition:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return kExitFailure;
    }
    auto ruledesc_it = rules_file.find(rule_name.GetNamedTarget().name);
    if (ruledesc_it == rules_file.end()) {
        Logger::Log(LogLevel::Error,
                    "Rule definition of {} is missing",
                    rule_name.ToString());
        return kExitFailure;
    }
    if (print_json) {
        PrintRuleAsOrderedJson(*ruledesc_it, rule_name.ToJson());
        return kExitSuccess;
    }
    PrettyPrintRule(*ruledesc_it, rule_name, repo_config);
    return kExitSuccess;
}

auto DescribeTarget(BuildMaps::Target::ConfiguredTarget const& id,
                    gsl::not_null<const RepositoryConfig*> const& repo_config,
                    std::size_t jobs,
                    bool print_json) -> int {
#ifndef BOOTSTRAP_BUILD_TOOL
    // check if target root is absent
    if (repo_config->TargetRoot(id.target.ToModule().repository)->IsAbsent()) {
        // check that we have a serve endpoint configured
        if (not RemoteServeConfig::RemoteAddress()) {
            Logger::Log(LogLevel::Error,
                        fmt::format("Root for target {} is absent but no serve "
                                    "endpoint was configured. Please provide "
                                    "--remote-serve-address and retry.",
                                    id.target.ToJson().dump()));
            return kExitFailure;
        }
        // check that just serve and the client use same remote execution
        // endpoint; it might make sense in the future to remove or avoid this
        // check, e.g., if remote endpoints are behind proxies.
        if (not ServeApi::CheckServeRemoteExecution()) {
            Logger::Log(LogLevel::Error,
                        "Inconsistent remote execution endpoint and serve "
                        "endpoint configuration detected.");
            return kExitFailure;
        }
        // ask serve endpoint to provide the description
        auto const& repo_name = id.target.ToModule().repository;
        auto target_root_id =
            repo_config->TargetRoot(repo_name)->GetAbsentTreeId();
        if (!target_root_id) {
            Logger::Log(
                LogLevel::Error,
                "Failed to get the target root id for repository \"{}\"",
                repo_name);
            return kExitFailure;
        }
        if (auto dgst = ServeApi::ServeTargetDescription(
                *target_root_id,
                *(repo_config->TargetFileName(repo_name)),
                id.target.GetNamedTarget().name)) {
            // if we're only asked to provide rule description as JSON, as this
            // is an export target, we don't need the blob and can directly
            // provide the user the information
            if (print_json) {
                std::cout << nlohmann::json({{"type", "export"}}).dump(2)
                          << std::endl;
                return kExitSuccess;
            }
            // get description from remote CAS
            auto const local_api = CreateExecutionApi(
                std::nullopt, std::make_optional(repo_config));
            auto const remote_api =
                CreateExecutionApi(RemoteExecutionConfig::RemoteAddress(),
                                   std::make_optional(repo_config));
            auto const& desc_info =
                Artifact::ObjectInfo{.digest = *dgst, .type = ObjectType::File};
            if (!local_api->IsAvailable(*dgst)) {
                if (!remote_api->RetrieveToCas({desc_info}, &*local_api)) {
                    Logger::Log(LogLevel::Error,
                                "Failed to retrieve blob {} from remote CAS",
                                desc_info.ToString());
                    return kExitFailure;
                }
            }
            auto const desc_str = local_api->RetrieveToMemory(desc_info);
            if (not desc_str) {
                Logger::Log(LogLevel::Error,
                            "Could not load in memory blob {}",
                            desc_info.ToString());
                return kExitFailure;
            }
            // parse blob into JSON object
            nlohmann::json desc;
            try {
                desc = nlohmann::json::parse(*desc_str);
            } catch (std::exception const& ex) {
                Logger::Log(
                    LogLevel::Error,
                    "Parsing served target description failed with:\n{}",
                    ex.what());
                return kExitFailure;
            }
            // serve endpoint already checked that this target is of
            // "type": "export", so we can just print the description
            std::cout << id.ToString()
                      << " is defined by built-in rule \"export\"."
                      << std::endl;
            auto doc = desc.find("doc");
            if (doc != desc.end()) {
                PrintDoc(*doc, " | ");
            }
            auto config_doc = nlohmann::json::object();
            auto config_doc_it = desc.find("config_doc");
            if (config_doc_it != desc.end() and config_doc_it->is_object()) {
                config_doc = *config_doc_it;
            }
            auto flexible_config = desc.find("flexible_config");
            if (flexible_config != desc.end() and
                (not flexible_config->empty())) {
                std::cout << " Flexible configuration variables\n";
                PrintFields(*flexible_config, config_doc, " - ", "   | ");
            }
            return kExitSuccess;
        }
        // report failure to serve description
        Logger::Log(LogLevel::Error,
                    "Serve endpoint could not provide description of target {} "
                    "with absent root.",
                    id.target.ToJson().dump());
        return kExitFailure;
    }
#endif  // BOOTSTRAP_BUILD_TOOL

    // process with a present target root
    auto targets_file_map = Base::CreateTargetsFileMap(repo_config, jobs);
    nlohmann::json targets_file{};
    bool failed{false};
    {
        TaskSystem ts{jobs};
        targets_file_map.ConsumeAfterKeysReady(
            &ts,
            {id.target.ToModule()},
            [&targets_file](auto values) { targets_file = *values[0]; },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While searching for target description:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return kExitFailure;
    }
    auto desc_it = targets_file.find(id.target.GetNamedTarget().name);
    if (desc_it == targets_file.end()) {
        std::cout << id.ToString() << " is implicitly a source file."
                  << std::endl;
        return kExitSuccess;
    }
    nlohmann::json desc = *desc_it;
    auto rule_it = desc.find("type");
    if (rule_it == desc.end()) {
        Logger::Log(LogLevel::Error,
                    "{} is a target without specified type.",
                    id.ToString());
        return kExitFailure;
    }
    if (BuildMaps::Target::IsBuiltInRule(*rule_it)) {
        if (print_json) {
            // For built-in rules, we have no user-defined description to
            // provide other than informing the user that it is a built-in rule.
            std::cout << nlohmann::json({{"type", *rule_it}}).dump(2)
                      << std::endl;
            return kExitSuccess;
        }
        std::cout << id.ToString() << " is defined by built-in rule "
                  << rule_it->dump() << "." << std::endl;
        if (*rule_it == "export") {
            // export targets may have doc fields of their own.
            auto doc = desc.find("doc");
            if (doc != desc.end()) {
                PrintDoc(*doc, " | ");
            }
            auto config_doc = nlohmann::json::object();
            auto config_doc_it = desc.find("config_doc");
            if (config_doc_it != desc.end() and config_doc_it->is_object()) {
                config_doc = *config_doc_it;
            }
            auto flexible_config = desc.find("flexible_config");
            if (flexible_config != desc.end() and
                (not flexible_config->empty())) {
                std::cout << " Flexible configuration variables\n";
                PrintFields(*flexible_config, config_doc, " - ", "   | ");
            }
        }
        return kExitSuccess;
    }
    auto rule_name = BuildMaps::Base::ParseEntityNameFromJson(
        *rule_it,
        id.target,
        repo_config,
        [&rule_it, &id](std::string const& parse_err) {
            Logger::Log(LogLevel::Error,
                        "Parsing rule name {} for target {} failed with:\n{}.",
                        rule_it->dump(),
                        id.ToString(),
                        parse_err);
        });
    if (not rule_name) {
        return kExitFailure;
    }
    if (not print_json) {
        std::cout << id.ToString() << " is defined by user-defined rule "
                  << rule_name->ToString() << ".\n\n";
    }
    return DescribeUserDefinedRule(*rule_name, repo_config, jobs, print_json);
}
