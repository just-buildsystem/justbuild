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

#include "src/buildtool/main/diagnose.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/json.hpp"

namespace {

namespace Base = BuildMaps::Base;
namespace Target = BuildMaps::Target;

[[nodiscard]] auto ResultToJson(TargetResult const& result) -> nlohmann::json {
    return nlohmann::ordered_json{
        {"artifacts",
         result.artifact_stage->ToJson(
             Expression::JsonMode::SerializeAllButNodes)},
        {"runfiles",
         result.runfiles->ToJson(Expression::JsonMode::SerializeAllButNodes)},
        {"provides",
         result.provides->ToJson(Expression::JsonMode::SerializeAllButNodes)}};
}

[[nodiscard]] auto TargetActionsToJson(AnalysedTargetPtr const& target)
    -> nlohmann::json {
    auto actions = nlohmann::json::array();
    std::for_each(target->Actions().begin(),
                  target->Actions().end(),
                  [&actions](auto const& action) {
                      actions.push_back(action->ToJson());
                  });
    return actions;
}

[[nodiscard]] auto TreesToJson(AnalysedTargetPtr const& target)
    -> nlohmann::json {
    auto trees = nlohmann::json::object();
    std::for_each(
        target->Trees().begin(),
        target->Trees().end(),
        [&trees](auto const& tree) { trees[tree->Id()] = tree->ToJson(); });

    return trees;
}

void DumpActions(std::string const& file_path, AnalysisResult const& result) {
    auto const dump_string =
        IndentListsOnlyUntilDepth(TargetActionsToJson(result.target), 2, 1);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Actions for target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping actions for target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpBlobs(std::string const& file_path, AnalysisResult const& result) {
    auto blobs = nlohmann::json::array();
    for (auto const& s : result.target->Blobs()) {
        blobs.push_back(s);
    }
    auto const dump_string = blobs.dump(2);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Blobs for target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping blobs for target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpVars(std::string const& file_path, AnalysisResult const& result) {
    auto vars = std::vector<std::string>{};
    vars.reserve(result.target->Vars().size());
    for (auto const& x : result.target->Vars()) {
        vars.push_back(x);
    }
    std::sort(vars.begin(), vars.end());
    auto const dump_string = nlohmann::json(vars).dump();
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Variables for target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping varables for target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpTrees(std::string const& file_path, AnalysisResult const& result) {
    auto const dump_string = TreesToJson(result.target).dump(2);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Trees for target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping trees for target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpProvides(std::string const& file_path, AnalysisResult const& result) {
    auto const dump_string =
        result.target->Result()
            .provides->ToJson(Expression::JsonMode::SerializeAllButNodes)
            .dump(2);
    if (file_path == "-") {
        Logger::Log(LogLevel::Info,
                    "Provides map for target {}:",
                    result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping provides map for target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpTargets(std::string const& file_path,
                 std::vector<Target::ConfiguredTarget> const& target_ids,
                 std::string const& target_qualifier = "") {
    auto repo_map = nlohmann::json::object();
    auto conf_list =
        [&repo_map](Base::EntityName const& ref) -> nlohmann::json& {
        if (ref.IsAnonymousTarget()) {
            auto const& anon = ref.GetAnonymousTarget();
            auto& anon_map = repo_map[Base::EntityName::kAnonymousMarker];
            auto& rule_map = anon_map[anon.rule_map.ToIdentifier()];
            return rule_map[anon.target_node.ToIdentifier()];
        }
        auto const& named = ref.GetNamedTarget();
        auto& location_map = repo_map[Base::EntityName::kLocationMarker];
        auto& module_map = location_map[named.repository];
        auto& target_map = module_map[named.module];
        return target_map[named.name];
    };
    std::for_each(
        target_ids.begin(), target_ids.end(), [&conf_list](auto const& id) {
            if ((not id.target.IsNamedTarget()) or
                id.target.GetNamedTarget().reference_t ==
                    BuildMaps::Base::ReferenceType::kTarget) {
                conf_list(id.target).push_back(id.config.ToJson());
            }
        });
    auto const dump_string = IndentListsOnlyUntilDepth(repo_map, 2);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "List of analysed {}targets:", target_qualifier);
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping list of analysed {}targets to file '{}'.",
                    target_qualifier,
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

auto DumpExpressionToMap(gsl::not_null<nlohmann::json*> const& map,
                         ExpressionPtr const& expr) -> bool {
    auto const& id = expr->ToIdentifier();
    if (not map->contains(id)) {
        (*map)[id] = expr->ToJson();
        return true;
    }
    return false;
}

void DumpNodesInExpressionToMap(gsl::not_null<nlohmann::json*> const& map,
                                ExpressionPtr const& expr) {
    if (expr->IsNode()) {
        if (DumpExpressionToMap(map, expr)) {
            auto const& node = expr->Node();
            if (node.IsAbstract()) {
                DumpNodesInExpressionToMap(map,
                                           node.GetAbstract().target_fields);
            }
            else if (node.IsValue()) {
                DumpNodesInExpressionToMap(map, node.GetValue());
            }
        }
    }
    else if (expr->IsList()) {
        for (auto const& entry : expr->List()) {
            DumpNodesInExpressionToMap(map, entry);
        }
    }
    else if (expr->IsMap()) {
        for (auto const& [_, value] : expr->Map()) {
            DumpNodesInExpressionToMap(map, value);
        }
    }
    else if (expr->IsResult()) {
        DumpNodesInExpressionToMap(map, expr->Result().provides);
    }
}

void DumpAnonymous(std::string const& file_path,
                   std::vector<Target::ConfiguredTarget> const& target_ids) {
    auto anon_map = nlohmann::json{{"nodes", nlohmann::json::object()},
                                   {"rule_maps", nlohmann::json::object()}};
    std::for_each(
        target_ids.begin(), target_ids.end(), [&anon_map](auto const& id) {
            if (id.target.IsAnonymousTarget()) {
                auto const& anon_t = id.target.GetAnonymousTarget();
                DumpExpressionToMap(&anon_map["rule_maps"], anon_t.rule_map);
                DumpNodesInExpressionToMap(&anon_map["nodes"],
                                           anon_t.target_node);
            }
        });
    auto const dump_string = IndentListsOnlyUntilDepth(anon_map, 2);
    if (file_path == "-") {
        Logger::Log(LogLevel::Info, "List of anonymous target data:");
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping list of anonymous target data to file '{}'.",
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpNodes(std::string const& file_path, AnalysisResult const& result) {
    auto node_map = nlohmann::json::object();
    DumpNodesInExpressionToMap(&node_map, result.target->Provides());
    auto const dump_string = IndentListsOnlyUntilDepth(node_map, 2);
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Target nodes of target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping target nodes of target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os(file_path);
        os << dump_string << std::endl;
    }
}

void DumpResult(std::string const& file_path, AnalysisResult const& result) {
    auto const dump_string = ResultToJson(result.target->Result()).dump();
    if (file_path == "-") {
        Logger::Log(
            LogLevel::Info, "Result of target {}:", result.id.ToString());
        std::cout << dump_string << std::endl;
    }
    else {
        Logger::Log(LogLevel::Info,
                    "Dumping result of target {} to file '{}'.",
                    result.id.ToString(),
                    file_path);
        std::ofstream os{file_path};
        os << dump_string << std::endl;
    }
}
}  // namespace

void DiagnoseResults(AnalysisResult const& result,
                     BuildMaps::Target::ResultTargetMap const& result_map,
                     DiagnosticArguments const& clargs) {
    Logger::Log(
        LogLevel::Info,
        "Result of{} target {}: {}",
        result.modified
            ? fmt::format(" input of action {} of", *result.modified)
            : "",
        result.id.ToString(),
        IndentOnlyUntilDepth(
            ResultToJson(result.target->Result()),
            2,
            2,
            std::unordered_map<std::string, std::size_t>{{"/provides", 3}}));
    if (clargs.dump_result) {
        DumpResult(*clargs.dump_result, result);
    }
    if (clargs.dump_actions) {
        DumpActions(*clargs.dump_actions, result);
    }
    if (clargs.dump_blobs) {
        DumpBlobs(*clargs.dump_blobs, result);
    }
    if (clargs.dump_trees) {
        DumpTrees(*clargs.dump_trees, result);
    }
    if (clargs.dump_provides) {
        DumpProvides(*clargs.dump_provides, result);
    }
    if (clargs.dump_vars) {
        DumpVars(*clargs.dump_vars, result);
    }
    if (clargs.dump_targets) {
        DumpTargets(*clargs.dump_targets, result_map.ConfiguredTargets());
    }
    if (clargs.dump_export_targets) {
        DumpTargets(
            *clargs.dump_export_targets, result_map.ExportTargets(), "export ");
    }
    if (clargs.dump_targets_graph) {
        auto graph = result_map.ConfiguredTargetsGraph().dump(2);
        Logger::Log(LogLevel::Info,
                    "Dumping graph of configured-targets to file {}.",
                    *clargs.dump_targets_graph);
        std::ofstream os(*clargs.dump_targets_graph);
        os << graph << std::endl;
    }
    if (clargs.dump_anonymous) {
        DumpAnonymous(*clargs.dump_anonymous, result_map.ConfiguredTargets());
    }
    if (clargs.dump_nodes) {
        DumpNodes(*clargs.dump_nodes, result);
    }
}
