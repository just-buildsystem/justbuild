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

#include <exception>
#include <vector>

#include <fmt/core.h>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"

auto TargetCache::Store(TargetCacheKey const& key,
                        TargetCacheEntry const& value) const noexcept -> bool {
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

auto TargetCache::Read(TargetCacheKey const& key) const noexcept
    -> std::optional<std::pair<TargetCacheEntry, Artifact::ObjectInfo>> {
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
                    return std::make_pair(
                        TargetCacheEntry{nlohmann::json::parse(*value)},
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

auto TargetCache::DownloadKnownArtifacts(
    TargetCacheEntry const& value) const noexcept -> bool {
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
