#include "src/buildtool/build_engine/expression/target_result.hpp"

#include <unordered_map>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
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

// Serialize arbitrary expression to JSON. This and any sub-expressions will be
// collected in `nodes`. Any possible duplicate will be collected only once. As
// pure JSON values can coincide with our JSON encoding of artifacts, the hash
// of artifact expressions is recorded in `provided_artifacts` to differentiate
// them from non-artifacts. If replacements is set, replace any contained
// non-known artifact by known artifact from replacement. Throws runtime_error
// if no replacement is found.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto SerializeExpression(
    gsl::not_null<std::unordered_map<std::string, nlohmann::json>*> const&
        nodes,
    gsl::not_null<std::vector<std::string>*> const& provided_artifacts,
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
                auto hash = SerializeExpression(
                    nodes, provided_artifacts, val, replacements);
                hashes[key] = std::move(hash);
            }
            json = std::move(hashes);
        }
        else if (expr->IsList()) {
            auto const& list = expr->List();
            std::vector<std::string> hashes{};
            hashes.reserve(list.size());
            for (auto const& val : list) {
                auto hash = SerializeExpression(
                    nodes, provided_artifacts, val, replacements);
                hashes.emplace_back(std::move(hash));
            }
            json = std::move(hashes);
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
    nlohmann::json const& provided_artifacts) -> ExpressionPtr {
    static auto contains = [](auto const& a, auto const& v) -> bool {
        return std::find(a.begin(), a.end(), v) != a.end();
    };
    auto id = entry.get<std::string>();
    auto const& json = nodes.at(id);
    if (json.is_object()) {
        if (contains(provided_artifacts, id)) {
            if (auto artifact = ArtifactDescription::FromJson(json)) {
                return ExpressionPtr{*artifact};
            }
            return ExpressionPtr{nullptr};
        }

        Expression::map_t::underlying_map_t map{};
        for (auto const& [key, val] : json.items()) {
            auto new_val = DeserializeExpression(
                val.get<std::string>(), nodes, provided_artifacts);
            if (not new_val) {
                return new_val;
            }
            map.emplace(key, std::move(new_val));
        }
        return ExpressionPtr{Expression::map_t{map}};
    }

    if (json.is_array()) {
        Expression::list_t list{};
        list.reserve(json.size());
        for (auto const& val : json) {
            auto new_val = DeserializeExpression(
                val.get<std::string>(), nodes, provided_artifacts);
            if (not new_val) {
                return new_val;
            }
            list.emplace_back(std::move(new_val));
        }
        return ExpressionPtr{list};
    }

    return Expression::FromJson(json);
}

// Serialize artifact map to JSON. If replacements is set, replace
// non-known artifacts by known artifacts from replacement. Throws runtime_error
// if no replacement is found.
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
[[nodiscard]] auto SerializeProvidesMap(
    ExpressionPtr const& expr,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) -> nlohmann::json {
    auto provided_artifacts = std::vector<std::string>{};
    auto nodes = std::unordered_map<std::string, nlohmann::json>{};
    auto entry =
        SerializeExpression(&nodes, &provided_artifacts, expr, replacements);
    return nlohmann::json{
        {"entry", std::move(entry)},
        {"nodes", std::move(nodes)},
        {"provided_artifacts", std::move(provided_artifacts)}};
}

[[nodiscard]] auto DeserializeProvidesMap(nlohmann::json const& json)
    -> ExpressionPtr {
    return DeserializeExpression(
        json["entry"], json["nodes"], json["provided_artifacts"]);
}

// Serialize TargetResult to JSON. If replacements is set, replace non-known
// artifacts by known artifacts from replacement. Throws runtime_error if no
// replacement is found.
[[nodiscard]] auto SerializeTargetResultWithReplacement(
    TargetResult const& result,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements = {}) -> nlohmann::json {
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
