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

#include "src/buildtool/storage/target_cache_entry.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/gsl.hpp"

auto TargetCacheEntry::FromTarget(
    HashFunction::Type hash_type,
    AnalysedTargetPtr const& target,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) noexcept -> std::optional<TargetCacheEntry> {
    auto result = TargetResult{.artifact_stage = target->Artifacts(),
                               .provides = target->Provides(),
                               .runfiles = target->RunFiles()};
    auto desc = result.ReplaceNonKnownAndToJson(replacements);
    if (not desc) {
        return std::nullopt;
    }
    std::vector<std::string> implied{};
    for (auto const& x : target->ImpliedExport()) {
        implied.emplace_back(x);
    }
    if (not implied.empty()) {
        (*desc)["implied export targets"] = implied;
    }
    return TargetCacheEntry{hash_type, *desc};
}

auto TargetCacheEntry::FromJson(HashFunction::Type hash_type,
                                nlohmann::json desc) noexcept
    -> TargetCacheEntry {
    return TargetCacheEntry(hash_type, std::move(desc));
}

auto TargetCacheEntry::ToResult() const noexcept
    -> std::optional<TargetResult> {
    return TargetResult::FromJson(hash_type_, desc_);
}

auto TargetCacheEntry::ToImplied() const noexcept -> std::set<std::string> {
    std::set<std::string> result{};
    if (desc_.contains("implied export targets")) {
        try {
            for (auto const& x : desc_["implied export targets"]) {
                result.emplace(x);
            }
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Warning,
                        "Exception reading implied export targets: {}",
                        ex.what());
        }
    }
    return result;
}

auto TargetCacheEntry::ToImpliedIds(std::string const& entry_key_hash)
    const noexcept -> std::optional<std::vector<Artifact::ObjectInfo>> {
    std::vector<Artifact::ObjectInfo> result{};
    if (desc_.contains("implied export targets")) {
        try {
            for (auto const& x : desc_["implied export targets"]) {
                if (x != entry_key_hash) {
                    auto digest = ArtifactDigestFactory::Create(
                        hash_type_, x, 0, /*is_tree=*/false);
                    if (not digest) {
                        Logger::Log(
                            LogLevel::Debug, "{}", std::move(digest).error());
                        return std::nullopt;
                    }
                    result.emplace_back(
                        Artifact::ObjectInfo{.digest = *std::move(digest),
                                             .type = ObjectType::File});
                }
            }
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Warning,
                        "Exception reading implied export targets: {}",
                        ex.what());
            return std::nullopt;
        }
    }
    return result;
}

[[nodiscard]] static auto ToObjectInfo(HashFunction::Type hash_type,
                                       nlohmann::json const& json)
    -> Artifact::ObjectInfo {
    auto const desc = ArtifactDescription::FromJson(hash_type, json);
    // The assumption is that all artifacts mentioned in a target cache
    // entry are KNOWN to the remote side.
    ExpectsAudit(desc and desc->IsKnown());
    auto const& info = desc->ToArtifact().Info();
    ExpectsAudit(info);
    return *info;
}

[[nodiscard]] static auto ScanArtifactMap(
    HashFunction::Type hash_type,
    gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& infos,
    nlohmann::json const& json) -> bool {
    if (not json.is_object()) {
        return false;
    }
    infos->reserve(infos->size() + json.size());
    std::transform(json.begin(),
                   json.end(),
                   std::back_inserter(*infos),
                   [hash_type](auto const& item) {
                       return ToObjectInfo(hash_type, item);
                   });
    return true;
}

[[nodiscard]] static auto ScanProvidesMap(
    HashFunction::Type hash_type,
    gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& infos,
    nlohmann::json const& json) -> bool {
    if (not json.is_object()) {
        return false;
    }
    auto const& nodes = json["nodes"];
    auto const& provided_artifacts = json["provided_artifacts"];
    infos->reserve(infos->size() + provided_artifacts.size());
    std::transform(provided_artifacts.begin(),
                   provided_artifacts.end(),
                   std::back_inserter(*infos),
                   [hash_type, &nodes](auto const& item) {
                       return ToObjectInfo(
                           hash_type, nodes[item.template get<std::string>()]);
                   });
    return true;
}

auto TargetCacheEntry::ToArtifacts(
    gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& infos)
    const noexcept -> bool {
    try {
        if (ScanArtifactMap(hash_type_, infos, desc_["artifacts"]) and
            ScanArtifactMap(hash_type_, infos, desc_["runfiles"]) and
            ScanProvidesMap(hash_type_, infos, desc_["provides"])) {
            return true;
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error,
            "Scanning target cache entry for artifacts failed with:\n{}",
            ex.what());
    }
    return false;
}
