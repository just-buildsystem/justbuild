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

#include <filesystem>
#include <vector>

#include "nlohmann/json.hpp"
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

namespace {

auto RemoveDirs(const std::vector<std::filesystem::path>& directories) -> bool {
    bool success = true;
    for (auto const& d : directories) {
        if (FileSystemManager::IsDirectory(d)) {
            if (not FileSystemManager::RemoveDirectory(d,
                                                       /*recursively=*/true)) {
                Logger::Log(LogLevel::Warning,
                            "Failed to remove directory {}",
                            d.string());
                success = false;  // We failed to remove all, but still try the
                                  // others to clean up as much as possible
            }
        }
    }
    return success;
}

}  // namespace

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

auto GarbageCollector::TriggerGarbageCollection(bool no_rotation) noexcept
    -> bool {
    auto const kRemoveMe = std::string{"remove-me"};

    auto pid = CreateProcessUniqueId();
    if (not pid) {
        return false;
    }
    auto remove_me_prefix = kRemoveMe + *pid + std::string{"-"};
    std::vector<std::filesystem::path> to_remove{};

    // With a shared lock, we can remove all directories with the given prefix,
    // as we own the process id.
    {
        auto lock = SharedLock();
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to get a shared lock the local build root");
            return false;
        }

        for (auto const& entry :
             std::filesystem::directory_iterator(StorageConfig::CacheRoot())) {
            if (entry.path().filename().string().find(remove_me_prefix) == 0) {
                to_remove.emplace_back(entry.path());
            }
        }
        if (not RemoveDirs(to_remove)) {
            Logger::Log(LogLevel::Error,
                        "Failed to clean up left-over directories under my "
                        "pid. Will not continue");
            return false;
        }
    }

    to_remove.clear();
    int remove_me_counter{};

    // after releasing the shared lock, wait to get an exclusive lock for doing
    // the critical renamings
    {
        auto lock = ExclusiveLock();
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to exclusively lock the local build root");
            return false;
        }

        // Frirst, while he have not yet created any to-remove directories, grab
        // all existing remove-me directories; they're left overs, as the clean
        // up of owned directories is done with a shared lock.
        std::vector<std::filesystem::path> left_over{};
        for (auto const& entry :
             std::filesystem::directory_iterator(StorageConfig::CacheRoot())) {
            if (entry.path().filename().string().find(kRemoveMe) == 0) {
                left_over.emplace_back(entry.path());
            }
        }
        for (auto const& d : left_over) {
            auto new_name =
                StorageConfig::CacheRoot() /
                fmt::format("{}{}", remove_me_prefix, remove_me_counter++);
            if (FileSystemManager::Rename(d, new_name)) {
                to_remove.emplace_back(new_name);
            }
            else {
                Logger::Log(LogLevel::Warning,
                            "Failed to rename {} to {}",
                            d.string(),
                            new_name.string());
            }
        }

        // Now that the have to exclusive lock, try to move out ephemeral data;
        // as it is still under the generation regime, it is not a huge problem
        // if that fails.
        auto ephemeral = StorageConfig::EphemeralRoot();
        if (FileSystemManager::IsDirectory(ephemeral)) {
            auto remove_me_dir =
                StorageConfig::CacheRoot() /
                fmt::format("{}{}", remove_me_prefix, remove_me_counter++);
            if (FileSystemManager::Rename(ephemeral, remove_me_dir)) {
                to_remove.emplace_back(remove_me_dir);
            }
            else {
                Logger::Log(LogLevel::Warning,
                            "Failed to rename {} to {}.",
                            ephemeral.string(),
                            remove_me_dir.string());
            }
        }
        // Rotate generations unless told not to do so
        if (not no_rotation) {
            auto remove_me_dir =
                StorageConfig::CacheRoot() /
                fmt::format("{}{}", remove_me_prefix, remove_me_counter++);
            to_remove.emplace_back(remove_me_dir);
            for (std::size_t i = StorageConfig::NumGenerations(); i > 0; --i) {
                auto cache_root = StorageConfig::GenerationCacheRoot(i - 1);
                if (FileSystemManager::IsDirectory(cache_root)) {
                    auto new_cache_root =
                        (i == StorageConfig::NumGenerations())
                            ? remove_me_dir
                            : StorageConfig::GenerationCacheRoot(i);
                    if (not FileSystemManager::Rename(cache_root,
                                                      new_cache_root)) {
                        Logger::Log(LogLevel::Error,
                                    "Failed to rename {} to {}.",
                                    cache_root.string(),
                                    new_cache_root.string());
                        return false;
                    }
                }
            }
        }
    }

    // After releasing the exlusive lock, get a shared lock and remove what we
    // have to remove
    bool success{};
    {
        auto lock = SharedLock();
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to get a shared lock the local build root");
            return false;
        }
        success = RemoveDirs(to_remove);
    }

    return success;
}

#endif  // BOOTSTRAP_BUILD_TOOL
