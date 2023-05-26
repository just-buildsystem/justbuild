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

#include "src/buildtool/build_engine/expression/target_result.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/logging/logger.hpp"

namespace {

// Serialize artifact description to JSON. If replacements is set, replace
// non-known artifacts by known artifacts from replacement. Throws
// bad_variant_access if expr is not an artifact or runtime_error if no
// replacement is found.
[[nodiscard]] auto SerializeArtifactDescription(
    ExpressionPtr const& expr,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) -> nlohmann::json {
    if (not replacements.empty()) {
        auto const& artifact = expr->Artifact();
        if (not expr->Artifact().IsKnown()) {
            if (auto it = replacements.find(artifact);
                it != replacements.end()) {
                auto const& info = it->second;
                return ArtifactDescription{info.digest, info.type}.ToJson();
            }
            throw std::runtime_error{
                "No replacement for non-known artifact found."};
        }
    }
    return expr->ToJson();
}

// forward declare as we have mutually recursive functions
auto SerializeTargetResultWithReplacement(
    TargetResult const& result,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements = {}) -> nlohmann::json;

// Serialize arbitrary expression to JSON. This and any sub-expressions will be
// collected in `nodes`. Any possible duplicate will be collected only once. As
// pure JSON values can coincide with our JSON encoding of artifacts, the hash
// of artifact expressions is recorded in `provided_artifacts` to differentiate
// them from non-artifacts. Similarly for result and node values.
// If replacements is set, replace any contained
// non-known artifact by known artifact from replacement. Throws runtime_error
// if no replacement is found.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto SerializeExpression(
    gsl::not_null<std::unordered_map<std::string, nlohmann::json>*> const&
        nodes,
    gsl::not_null<std::vector<std::string>*> const& provided_artifacts,
    gsl::not_null<std::vector<std::string>*> const& provided_nodes,
    gsl::not_null<std::vector<std::string>*> const& provided_results,
    ExpressionPtr const& expr,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) -> std::string {
    auto id = ToHexString(expr->ToHash());
    if (not nodes->contains(id)) {
        auto json = nlohmann::json();
        if (expr->IsMap()) {
            auto const& map = expr->Map();
            std::unordered_map<std::string, std::string> hashes{};
            hashes.reserve(map.size());
            for (auto const& [key, val] : map) {
                auto hash = SerializeExpression(nodes,
                                                provided_artifacts,
                                                provided_nodes,
                                                provided_results,
                                                val,
                                                replacements);
                hashes[key] = std::move(hash);
            }
            json = std::move(hashes);
        }
        else if (expr->IsList()) {
            auto const& list = expr->List();
            std::vector<std::string> hashes{};
            hashes.reserve(list.size());
            for (auto const& val : list) {
                auto hash = SerializeExpression(nodes,
                                                provided_artifacts,
                                                provided_nodes,
                                                provided_results,
                                                val,
                                                replacements);
                hashes.emplace_back(std::move(hash));
            }
            json = std::move(hashes);
        }
        else if (expr->IsNode()) {
            auto const& node = expr->Node();
            provided_nodes->emplace_back(id);
            if (node.IsValue()) {
                auto hash = SerializeExpression(nodes,
                                                provided_artifacts,
                                                provided_nodes,
                                                provided_results,
                                                node.GetValue(),
                                                replacements);
                json = nlohmann::json{{"type", "VALUE_NODE"}, {"result", hash}};
            }
            else {
                auto const& data = node.GetAbstract();
                auto string_fields = SerializeExpression(nodes,
                                                         provided_artifacts,
                                                         provided_nodes,
                                                         provided_results,
                                                         data.string_fields,
                                                         replacements);
                auto target_fields = SerializeExpression(nodes,
                                                         provided_artifacts,
                                                         provided_nodes,
                                                         provided_results,
                                                         data.target_fields,
                                                         replacements);
                json = nlohmann::json{{"type", "ABSTRACT_NODE"},
                                      {"node_type", data.node_type},
                                      {"string_fields", string_fields},
                                      {"target_fields", target_fields}};
            }
        }
        else if (expr->IsResult()) {
            provided_results->emplace_back(id);
            auto const& result = expr->Result();
            auto artifact_stage = SerializeExpression(nodes,
                                                      provided_artifacts,
                                                      provided_nodes,
                                                      provided_results,
                                                      result.artifact_stage,
                                                      replacements);
            auto runfiles = SerializeExpression(nodes,
                                                provided_artifacts,
                                                provided_nodes,
                                                provided_results,
                                                result.runfiles,
                                                replacements);
            auto provides = SerializeExpression(nodes,
                                                provided_artifacts,
                                                provided_nodes,
                                                provided_results,
                                                result.provides,
                                                replacements);
            json = nlohmann::json({{"artifact_stage", artifact_stage},
                                   {"runfiles", runfiles},
                                   {"provides", provides}});
        }
        else if (expr->IsArtifact()) {
            provided_artifacts->emplace_back(id);
            json = SerializeArtifactDescription(expr, replacements);
        }
        else {
            json = expr->ToJson();
        }

        (*nodes)[id] = std::move(json);
    }
    return id;
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto DeserializeExpression(
    nlohmann::json const& entry,
    nlohmann::json const& nodes,
    std::unordered_set<std::string> const& provided_artifacts,
    std::unordered_set<std::string> const& provided_nodes,
    std::unordered_set<std::string> const& provided_results,
    gsl::not_null<std::unordered_map<std::string, ExpressionPtr>*> const& sofar)
    -> ExpressionPtr {

    auto id = entry.get<std::string>();

    auto it = sofar->find(id);
    if (it != sofar->end()) {
        return it->second;
    }

    auto const& json = nodes.at(id);
    if (json.is_object()) {
        if (provided_artifacts.contains(id)) {
            if (auto artifact = ArtifactDescription::FromJson(json)) {
                auto result = ExpressionPtr{*artifact};
                sofar->emplace(id, result);
                return result;
            }
            return ExpressionPtr{nullptr};
        }
        if (provided_nodes.contains(id)) {
            if (json["type"] == "ABSTRACT_NODE") {
                auto node_type = json["node_type"].get<std::string>();
                auto target_fields =
                    DeserializeExpression(json["target_fields"],
                                          nodes,
                                          provided_artifacts,
                                          provided_nodes,
                                          provided_results,
                                          sofar);
                auto string_fields =
                    DeserializeExpression(json["string_fields"],
                                          nodes,
                                          provided_artifacts,
                                          provided_nodes,
                                          provided_results,
                                          sofar);
                auto result = ExpressionPtr{TargetNode{
                    TargetNode::Abstract{.node_type = node_type,
                                         .string_fields = string_fields,
                                         .target_fields = target_fields}}};
                sofar->emplace(id, result);
                return result;
            }
            if (json["type"] == "VALUE_NODE") {
                auto value = DeserializeExpression(json["result"],
                                                   nodes,
                                                   provided_artifacts,
                                                   provided_nodes,
                                                   provided_results,
                                                   sofar);
                auto result = ExpressionPtr{TargetNode{value}};
                sofar->emplace(id, result);
                return result;
            }
            return ExpressionPtr{nullptr};
        }
        if (provided_results.contains(id)) {
            auto artifact_stage = DeserializeExpression(json["artifact_stage"],
                                                        nodes,
                                                        provided_artifacts,
                                                        provided_nodes,
                                                        provided_results,
                                                        sofar);
            auto runfiles = DeserializeExpression(json["runfiles"],
                                                  nodes,
                                                  provided_artifacts,
                                                  provided_nodes,
                                                  provided_results,
                                                  sofar);
            auto provides = DeserializeExpression(json["provides"],
                                                  nodes,
                                                  provided_artifacts,
                                                  provided_nodes,
                                                  provided_results,
                                                  sofar);
            if (artifact_stage and runfiles and provides) {
                return ExpressionPtr{
                    TargetResult{.artifact_stage = std::move(artifact_stage),
                                 .provides = std::move(provides),
                                 .runfiles = std::move(runfiles),
                                 .is_cacheable = true}};
            }
            return ExpressionPtr{nullptr};
        }

        Expression::map_t::underlying_map_t map{};
        for (auto const& [key, val] : json.items()) {
            auto new_val = DeserializeExpression(val.get<std::string>(),
                                                 nodes,
                                                 provided_artifacts,
                                                 provided_nodes,
                                                 provided_results,
                                                 sofar);
            if (not new_val) {
                return new_val;
            }
            map.emplace(key, std::move(new_val));
        }
        auto result = ExpressionPtr{Expression::map_t{map}};
        sofar->emplace(id, result);
        return result;
    }

    if (json.is_array()) {
        Expression::list_t list{};
        list.reserve(json.size());
        for (auto const& val : json) {
            auto new_val = DeserializeExpression(val.get<std::string>(),
                                                 nodes,
                                                 provided_artifacts,
                                                 provided_nodes,
                                                 provided_results,
                                                 sofar);
            if (not new_val) {
                return new_val;
            }
            list.emplace_back(std::move(new_val));
        }
        auto result = ExpressionPtr{list};
        sofar->emplace(id, result);
        return result;
    }

    auto result = Expression::FromJson(json);
    sofar->emplace(id, result);
    return result;
}

// Serialize artifact map to JSON. If replacements is set, replace
// non-known artifacts by known artifacts from replacement. Throws runtime_error
// if no replacement is found.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto SerializeArtifactMap(
    ExpressionPtr const& expr,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) -> nlohmann::json {
    if (replacements.empty()) {
        return expr->ToJson();
    }

    auto const& map = expr->Map();
    std::unordered_map<std::string, nlohmann::json> artifacts{};
    artifacts.reserve(map.size());
    for (auto const& [key, val] : map) {
        artifacts[key] = SerializeArtifactDescription(val, replacements);
    }
    return artifacts;
}

[[nodiscard]] auto DeserializeArtifactMap(nlohmann::json const& json)
    -> ExpressionPtr {
    if (json.is_object()) {
        Expression::map_t::underlying_map_t map{};
        for (auto const& [key, val] : json.items()) {
            auto artifact = ArtifactDescription::FromJson(val);
            if (not artifact) {
                return ExpressionPtr{nullptr};
            }
            map.emplace(key, ExpressionPtr{std::move(*artifact)});
        }
        return ExpressionPtr{Expression::map_t{map}};
    }
    return ExpressionPtr{nullptr};
}

// Serialize provides map to JSON. If replacements is set, replace
// non-known artifacts by known artifacts from replacement. Throws runtime_error
// if no replacement is found.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto SerializeProvidesMap(
    ExpressionPtr const& expr,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) -> nlohmann::json {
    auto provided_artifacts = std::vector<std::string>{};
    auto provided_nodes = std::vector<std::string>{};
    auto provided_results = std::vector<std::string>{};
    auto nodes = std::unordered_map<std::string, nlohmann::json>{};
    auto entry = SerializeExpression(&nodes,
                                     &provided_artifacts,
                                     &provided_nodes,
                                     &provided_results,
                                     expr,
                                     replacements);
    return nlohmann::json{{"entry", std::move(entry)},
                          {"nodes", std::move(nodes)},
                          {"provided_artifacts", std::move(provided_artifacts)},
                          {"provided_nodes", std::move(provided_nodes)},
                          {"provided_results", std::move(provided_results)}

    };
}

auto JsonSet(nlohmann::json const& j) -> std::unordered_set<std::string> {
    std::unordered_set<std::string> result{};
    result.reserve(j.size());
    for (auto const& it : j) {
        result.emplace(it.get<std::string>());
    }
    return result;
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto DeserializeProvidesMap(nlohmann::json const& json)
    -> ExpressionPtr {
    std::unordered_map<std::string, ExpressionPtr> sofar{};
    return DeserializeExpression(json["entry"],
                                 json["nodes"],
                                 JsonSet(json["provided_artifacts"]),
                                 JsonSet(json["provided_nodes"]),
                                 JsonSet(json["provided_results"]),
                                 &sofar);
}

// Serialize TargetResult to JSON. If replacements is set, replace non-known
// artifacts by known artifacts from replacement. Throws runtime_error if no
// replacement is found.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto SerializeTargetResultWithReplacement(
    TargetResult const& result,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) -> nlohmann::json {
    return nlohmann::json{
        {"artifacts",
         SerializeArtifactMap(result.artifact_stage, replacements)},
        {"runfiles", SerializeArtifactMap(result.runfiles, replacements)},
        {"provides", SerializeProvidesMap(result.provides, replacements)}};
}

}  // namespace

auto TargetResult::ToJson() const -> nlohmann::json {
    return SerializeTargetResultWithReplacement(*this /* no replacement */);
}

auto TargetResult::ReplaceNonKnownAndToJson(
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) const noexcept -> std::optional<nlohmann::json> {
    try {
        return SerializeTargetResultWithReplacement(*this, replacements);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "Serializing target result to JSON failed with:\n{}",
                    ex.what());
    }
    return std::nullopt;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto TargetResult::FromJson(nlohmann::json const& json) noexcept
    -> std::optional<TargetResult> {
    try {
        auto artifacts = DeserializeArtifactMap(json["artifacts"]);
        auto runfiles = DeserializeArtifactMap(json["runfiles"]);
        auto provides = DeserializeProvidesMap(json["provides"]);
        if (artifacts and runfiles and provides) {
            return TargetResult{artifacts, provides, runfiles};
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "Deserializing target result failed with:\n{}",
                    ex.what());
    }
    return std::nullopt;
}
