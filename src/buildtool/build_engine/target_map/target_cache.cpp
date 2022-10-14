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

#include "src/buildtool/build_engine/target_map/target_cache.hpp"

#include <gsl-lite/gsl-lite.hpp>

#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/file_storage.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

auto TargetCache::Key::Create(BuildMaps::Base::EntityName const& target,
                              Configuration const& effective_config) noexcept
    -> std::optional<TargetCache::Key> {
    static auto const& repos = RepositoryConfig::Instance();
    try {
        if (auto repo_key =
                repos.RepositoryKey(target.GetNamedTarget().repository)) {
            // target's repository is content-fixed, we can compute a cache key
            auto const& name = target.GetNamedTarget();
            auto target_desc = nlohmann::json{
                {{"repo_key", *repo_key},
                 {"target_name", nlohmann::json{name.module, name.name}.dump()},
                 {"effective_config", effective_config.ToString()}}};
            static auto const& cas = LocalCAS<ObjectType::File>::Instance();
            if (auto target_key = cas.StoreBlobFromBytes(target_desc.dump(2))) {
                return Key{{ArtifactDigest{*target_key}, ObjectType::File}};
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "Creating target cache key failed with:\n{}",
                    ex.what());
    }
    return std::nullopt;
}

auto TargetCache::Entry::FromTarget(
    AnalysedTargetPtr const& target,
    std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
        replacements) noexcept -> std::optional<TargetCache::Entry> {
    auto result = TargetResult{
        target->Artifacts(), target->Provides(), target->RunFiles()};
    if (auto desc = result.ReplaceNonKnownAndToJson(replacements)) {
        return Entry{*desc};
    }
    return std::nullopt;
}

auto TargetCache::Entry::ToResult() const noexcept
    -> std::optional<TargetResult> {
    return TargetResult::FromJson(desc_);
}

[[nodiscard]] auto ToObjectInfo(nlohmann::json const& json)
    -> Artifact::ObjectInfo {
    auto const& desc = ArtifactDescription::FromJson(json);
    // The assumption is that all artifacts mentioned in a target cache
    // entry are KNOWN to the remote side.
    gsl_ExpectsAudit(desc and desc->IsKnown());
    auto const& info = desc->ToArtifact().Info();
    gsl_ExpectsAudit(info);
    return *info;
}

[[nodiscard]] auto ScanArtifactMap(
    gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& infos,
    nlohmann::json const& json) -> bool {
    if (not json.is_object()) {
        return false;
    }
    infos->reserve(infos->size() + json.size());
    std::transform(json.begin(),
                   json.end(),
                   std::back_inserter(*infos),
                   [](auto const& item) { return ToObjectInfo(item); });
    return true;
}

// Function prototype for recursive call in ScanProvidesMap
auto ScanTargetResult(
    gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& /*infos*/,
    nlohmann::json const& /*json*/) -> bool;

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto ScanProvidesMap(
    gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& infos,
    nlohmann::json const& json) -> bool {
    if (not json.is_object()) {
        return false;
    }
    auto const& nodes = json["nodes"];

    // Process provided artifacts
    auto const& provided_artifacts = json["provided_artifacts"];
    infos->reserve(infos->size() + provided_artifacts.size());
    std::transform(
        provided_artifacts.begin(),
        provided_artifacts.end(),
        std::back_inserter(*infos),
        [&nodes](auto const& item) {
            return ToObjectInfo(nodes[item.template get<std::string>()]);
        });

    // Process provided results
    auto const& provided_results = json["provided_results"];
    return std::all_of(provided_results.begin(),
                       provided_results.end(),
                       // NOLINTNEXTLINE(misc-no-recursion)
                       [&infos, &nodes](auto const& item) {
                           return ScanTargetResult(
                               infos, nodes[item.template get<std::string>()]);
                       });
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto ScanTargetResult(
    gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& infos,
    nlohmann::json const& result) -> bool {
    return ScanArtifactMap(infos, result["artifacts"]) and
           ScanArtifactMap(infos, result["runfiles"]) and
           ScanProvidesMap(infos, result["provides"]);
}

auto TargetCache::Entry::ToArtifacts(
    gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& infos)
    const noexcept -> bool {
    try {
        if (ScanTargetResult(infos, desc_)) {
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

auto TargetCache::Store(Key const& key, Entry const& value) const noexcept
    -> bool {
    // Before a target-cache entry is stored in local CAS, make sure any created
    // artifact for this target is downloaded from the remote CAS to the local
    // CAS.
    if (not DownloadKnownArtifacts(value)) {
        return false;
    }
    if (auto digest = CAS().StoreBlobFromBytes(value.ToJson().dump(2))) {
        auto data =
            Artifact::ObjectInfo{ArtifactDigest{*digest}, ObjectType::File}
                .ToString();
        logger_.Emit(LogLevel::Debug,
                     "Adding entry for key {} as {}",
                     key.Id().ToString(),
                     data);
        return file_store_.AddFromBytes(key.Id().digest.hash(), data);
    }
    return false;
}

auto TargetCache::Read(Key const& key) const noexcept
    -> std::optional<std::pair<Entry, Artifact::ObjectInfo>> {
    auto entry_path = file_store_.GetPath(key.Id().digest.hash());
    auto const entry =
        FileSystemManager::ReadFile(entry_path, ObjectType::File);
    if (not entry.has_value()) {
        logger_.Emit(LogLevel::Debug,
                     "Cache miss, entry not found {}",
                     entry_path.string());
        return std::nullopt;
    }
    if (auto info = Artifact::ObjectInfo::FromString(*entry)) {
        if (auto path = CAS().BlobPath(info->digest)) {
            if (auto value = FileSystemManager::ReadFile(*path)) {
                try {
                    return std::make_pair(Entry{nlohmann::json::parse(*value)},
                                          std::move(*info));
                } catch (std::exception const& ex) {
                    logger_.Emit(LogLevel::Warning,
                                 "Parsing entry for key {} failed with:\n{}",
                                 key.Id().ToString(),
                                 ex.what());
                }
            }
        }
    }
    logger_.Emit(LogLevel::Warning,
                 "Reading entry for key {} failed",
                 key.Id().ToString());
    return std::nullopt;
}

auto TargetCache::DownloadKnownArtifacts(Entry const& value) const noexcept
    -> bool {
    std::vector<Artifact::ObjectInfo> artifacts_info;
    if (not value.ToArtifacts(&artifacts_info)) {
        return false;
    }
#ifndef BOOTSTRAP_BUILD_TOOL
    // Sync KNOWN artifacts from remote to local CAS.
    return remote_api_->RetrieveToCas(artifacts_info, local_api_);
#else
    return true;
#endif
}

auto TargetCache::ComputeCacheDir() -> std::filesystem::path {
    return LocalExecutionConfig::TargetCacheDir() / ExecutionBackendId();
}

auto TargetCache::ExecutionBackendId() -> std::string {
    auto address = RemoteExecutionConfig::RemoteAddress();
    auto properties = RemoteExecutionConfig::PlatformProperties();
    auto backend_desc = nlohmann::json{
        {"remote_address",
         address ? nlohmann::json{fmt::format(
                       "{}:{}", address->host, address->port)}
                 : nlohmann::json{}},
        {"platform_properties",
         properties}}.dump(2);
    return NativeSupport::Unprefix(
        CAS().StoreBlobFromBytes(backend_desc).value().hash());
}
