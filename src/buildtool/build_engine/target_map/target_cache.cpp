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
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/execution_api/local/garbage_collector.hpp"
#endif

auto TargetCache::Store(TargetCacheKey const& key,
                        TargetCacheEntry const& value,
                        ArtifactDownloader const& downloader) const noexcept
    -> bool {
    // Before a target-cache entry is stored in local CAS, make sure any created
    // artifact for this target is downloaded from the remote CAS to the local
    // CAS.
    if (not DownloadKnownArtifacts(value, downloader)) {
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
    -> std::optional<std::pair<TargetCacheEntry, Artifact::ObjectInfo> > {
    auto id = key.Id().digest.hash();
    auto entry_path = file_store_.GetPath(id);
#ifndef BOOTSTRAP_BUILD_TOOL
    // Try to find target-cache entry in CAS generations and uplink if required.
    auto found = GarbageCollector::FindAndUplinkTargetCacheEntry(id);
#else
    auto found = FileSystemManager::IsFile(entry_path);
#endif
    if (not found) {
        logger_.Emit(LogLevel::Debug,
                     "Cache miss, entry not found {}",
                     entry_path.string());
        return std::nullopt;
    }
    auto const entry =
        FileSystemManager::ReadFile(entry_path, ObjectType::File);
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
    TargetCacheEntry const& value,
    ArtifactDownloader const& downloader) const noexcept -> bool {
    std::vector<Artifact::ObjectInfo> artifacts_info;
    return downloader and value.ToArtifacts(&artifacts_info) and
           downloader(artifacts_info);
}

auto TargetCache::ComputeCacheDir(int index) -> std::filesystem::path {
    [[maybe_unused]] auto id = CAS().StoreBlobFromBytes(
        LocalExecutionConfig::ExecutionBackendDescription());
    return LocalExecutionConfig::TargetCacheDir(index);
}
