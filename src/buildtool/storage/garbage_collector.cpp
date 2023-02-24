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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/storage/garbage_collector.hpp"

#include <vector>

#include <nlohmann/json.hpp>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"
#include "src/utils/cpp/hex_string.hpp"

auto GarbageCollector::GlobalUplinkBlob(bazel_re::Digest const& digest,
                                        bool is_executable) noexcept -> bool {
    // Try to find blob in all generations.
    auto const& latest_cas = Storage::Generation(0).CAS();
    for (std::size_t i = 0; i < StorageConfig::NumGenerations(); ++i) {
        // Note that we uplink with _skip_sync_ as we want to prefer hard links
        // from older generations over copies from the companion file/exec CAS.
        if (Storage::Generation(i).CAS().LocalUplinkBlob(
                latest_cas, digest, is_executable, /*skip_sync=*/true)) {
            return true;
        }
    }
    return false;
}

auto GarbageCollector::GlobalUplinkTree(bazel_re::Digest const& digest) noexcept
    -> bool {
    // Try to find tree in all generations.
    auto const& latest_cas = Storage::Generation(0).CAS();
    for (std::size_t i = 0; i < StorageConfig::NumGenerations(); ++i) {
        if (Storage::Generation(i).CAS().LocalUplinkTree(latest_cas, digest)) {
            return true;
        }
    }
    return false;
}

auto GarbageCollector::GlobalUplinkActionCacheEntry(
    bazel_re::Digest const& action_id) noexcept -> bool {
    // Try to find action-cache entry in all generations.
    auto const& latest_ac = Storage::Generation(0).ActionCache();
    for (std::size_t i = 0; i < StorageConfig::NumGenerations(); ++i) {
        if (Storage::Generation(i).ActionCache().LocalUplinkEntry(latest_ac,
                                                                  action_id)) {
            return true;
        }
    }
    return false;
}

auto GarbageCollector::GlobalUplinkTargetCacheEntry(
    TargetCacheKey const& key) noexcept -> bool {
    // Try to find target-cache entry in all generations.
    auto const& latest_tc = Storage::Generation(0).TargetCache();
    for (std::size_t i = 0; i < StorageConfig::NumGenerations(); ++i) {
        if (Storage::Generation(i).TargetCache().LocalUplinkEntry(latest_tc,
                                                                  key)) {
            return true;
        }
    }
    return false;
}

auto GarbageCollector::SharedLock() noexcept -> std::optional<LockFile> {
    return LockFile::Acquire(LockFilePath(), /*is_shared=*/true);
}

auto GarbageCollector::ExclusiveLock() noexcept -> std::optional<LockFile> {
    return LockFile::Acquire(LockFilePath(), /*is_shared=*/false);
}

auto GarbageCollector::LockFilePath() noexcept -> std::filesystem::path {
    return StorageConfig::CacheRoot() / "gc.lock";
}

auto GarbageCollector::TriggerGarbageCollection() noexcept -> bool {
    auto pid = CreateProcessUniqueId();
    if (not pid) {
        return false;
    }
    auto remove_me = std::string{"remove-me-"} + *pid;
    auto remove_me_dir = StorageConfig::CacheRoot() / remove_me;
    if (FileSystemManager::IsDirectory(remove_me_dir)) {
        if (not FileSystemManager::RemoveDirectory(remove_me_dir,
                                                   /*recursively=*/true)) {
            Logger::Log(LogLevel::Error,
                        "Failed to remove directory {}",
                        remove_me_dir.string());
            return false;
        }
    }
    {  // Create scope for critical renaming section protected by advisory lock.
        auto lock = ExclusiveLock();
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to exclusively lock the local build root");
            return false;
        }
        for (std::size_t i = StorageConfig::NumGenerations(); i > 0; --i) {
            auto cache_root = StorageConfig::GenerationCacheRoot(i - 1);
            if (FileSystemManager::IsDirectory(cache_root)) {
                auto new_cache_root =
                    (i == StorageConfig::NumGenerations())
                        ? remove_me_dir
                        : StorageConfig::GenerationCacheRoot(i);
                if (not FileSystemManager::Rename(cache_root, new_cache_root)) {
                    Logger::Log(LogLevel::Error,
                                "Failed to rename {} to {}.",
                                cache_root.string(),
                                new_cache_root.string());
                    return false;
                }
            }
        }
    }
    if (FileSystemManager::IsDirectory(remove_me_dir)) {
        if (not FileSystemManager::RemoveDirectory(remove_me_dir,
                                                   /*recursively=*/true)) {
            Logger::Log(LogLevel::Warning,
                        "Failed to remove directory {}",
                        remove_me_dir.string());
            return false;
        }
    }
    return true;
}

#endif  // BOOTSTRAP_BUILD_TOOL
