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

#include <array>
#include <filesystem>
#include <vector>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/compactifier.hpp"
#include "src/buildtool/storage/storage.hpp"

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

auto GarbageCollector::SharedLock(StorageConfig const& storage_config) noexcept
    -> std::optional<LockFile> {
    return LockFile::Acquire(LockFilePath(storage_config), /*is_shared=*/true);
}

auto GarbageCollector::ExclusiveLock(
    StorageConfig const& storage_config) noexcept -> std::optional<LockFile> {
    return LockFile::Acquire(LockFilePath(storage_config), /*is_shared=*/false);
}

auto GarbageCollector::LockFilePath(
    StorageConfig const& storage_config) noexcept -> std::filesystem::path {
    return storage_config.CacheRoot() / "gc.lock";
}

auto GarbageCollector::TriggerGarbageCollection(
    StorageConfig const& storage_config,
    bool no_rotation) noexcept -> bool {
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
        auto lock = SharedLock(storage_config);
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to get a shared lock the local build root");
            return false;
        }

        for (auto const& entry :
             std::filesystem::directory_iterator(storage_config.CacheRoot())) {
            if (entry.path().filename().string().starts_with(
                    remove_me_prefix)) {
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
    // the critical renaming
    {
        auto lock = ExclusiveLock(storage_config);
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to exclusively lock the local build root");
            return false;
        }

        // First, while he have not yet created any to-remove directories, grab
        // all existing remove-me directories; they're left overs, as the clean
        // up of owned directories is done with a shared lock.
        std::vector<std::filesystem::path> left_over{};
        for (auto const& entry :
             std::filesystem::directory_iterator(storage_config.CacheRoot())) {
            if (entry.path().filename().string().starts_with(kRemoveMe)) {
                left_over.emplace_back(entry.path());
            }
        }
        for (auto const& d : left_over) {
            auto new_name =
                storage_config.CacheRoot() /
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
        auto ephemeral = storage_config.EphemeralRoot();
        if (FileSystemManager::IsDirectory(ephemeral)) {
            auto remove_me_dir =
                storage_config.CacheRoot() /
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

        // Compactification must take place before rotating generations.
        // Otherwise, an interruption of the process during compactification
        // would lead to an invalid old generation.
        if (not GarbageCollector::Compactify(storage_config,
                                             kMaxBatchTransferSize)) {
            Logger::Log(LogLevel::Error,
                        "Failed to compactify the youngest generation.");
            return false;
        }

        // Rotate generations unless told not to do so
        if (not no_rotation) {
            auto remove_me_dir =
                storage_config.CacheRoot() /
                fmt::format("{}{}", remove_me_prefix, remove_me_counter++);
            to_remove.emplace_back(remove_me_dir);
            for (std::size_t i = storage_config.num_generations; i > 0; --i) {
                auto cache_root = storage_config.GenerationCacheRoot(i - 1);
                if (FileSystemManager::IsDirectory(cache_root)) {
                    auto new_cache_root =
                        (i == storage_config.num_generations)
                            ? remove_me_dir
                            : storage_config.GenerationCacheRoot(i);
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

    // After releasing the exclusive lock, get a shared lock and remove what we
    // have to remove
    bool success{};
    {
        auto lock = SharedLock(storage_config);
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to get a shared lock the local build root");
            return false;
        }
        success = RemoveDirs(to_remove);
    }

    return success;
}

auto GarbageCollector::Compactify(StorageConfig const& storage_config,
                                  size_t threshold) noexcept -> bool {
    // Compactification must be done for both native and compatible storages.
    static constexpr std::array kHashes = {HashFunction::Type::GitSHA1,
                                           HashFunction::Type::PlainSHA256};
    auto builder = StorageConfig::Builder{}
                       .SetBuildRoot(storage_config.build_root)
                       .SetNumGenerations(storage_config.num_generations);

    return std::all_of(
        kHashes.begin(),
        kHashes.end(),
        [threshold, &builder](HashFunction::Type hash_type) {
            auto const config = builder.SetHashType(hash_type).Build();
            if (not config) {
                return false;
            }

            auto const storage = ::Generation::Create(&*config);
            return Compactifier::RemoveInvalid(storage.CAS()) and
                   Compactifier::RemoveSpliced(storage.CAS()) and
                   Compactifier::SplitLarge(storage.CAS(), threshold);
        });
}

#endif  // BOOTSTRAP_BUILD_TOOL
