// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/storage/repository_garbage_collector.hpp"

#include <cstddef>
#include <string>

#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto RepositoryGarbageCollector::SharedLock(
    StorageConfig const& storage_config) noexcept -> std::optional<LockFile> {
    return LockFile::Acquire(LockFilePath(storage_config), /*is_shared=*/true);
}

auto RepositoryGarbageCollector::ExclusiveLock(
    StorageConfig const& storage_config) noexcept -> std::optional<LockFile> {
    return LockFile::Acquire(LockFilePath(storage_config), /*is_shared=*/false);
}

auto RepositoryGarbageCollector::LockFilePath(
    StorageConfig const& storage_config) noexcept -> std::filesystem::path {
    return storage_config.RepositoryRoot() / "gc.lock";
}

auto RepositoryGarbageCollector::TriggerGarbageCollection(
    StorageConfig const& storage_config) noexcept -> bool {
    auto const remove_me_prefix = std::string{"remove-me"};

    auto pid = CreateProcessUniqueId();
    if (not pid) {
        return false;
    }
    auto remove_me =
        storage_config.RepositoryRoot() / (remove_me_prefix + *pid);

    // With a shared lock, we can remove that directory, if it exists,
    // as we own the process id.
    {
        auto lock = SharedLock(storage_config);
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to get a shared lock the for repository root");
            return false;
        }
        if (FileSystemManager::IsDirectory(remove_me)) {
            if (not FileSystemManager::RemoveDirectory(remove_me,
                                                       /*recursively=*/true)) {
                Logger::Log(LogLevel::Error,
                            "Failed to remove directory {}",
                            remove_me.string());
                return false;
            }
        }
        else {
            if (not FileSystemManager::RemoveFile(remove_me)) {
                Logger::Log(
                    LogLevel::Error, "Failed to remove {}", remove_me.string());
                return false;
            }
        }
    }

    // after releasing the shared lock, wait to get an exclusive lock for doing
    // the critical renaming
    {
        auto lock = ExclusiveLock(storage_config);
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to exclusively lock the local repository root");
            return false;
        }

        for (std::size_t i = storage_config.num_generations; i > 0; i--) {
            auto from = storage_config.RepositoryGenerationRoot(i - 1);
            auto to = i < storage_config.num_generations
                          ? storage_config.RepositoryGenerationRoot(i)
                          : remove_me;
            if (FileSystemManager::IsDirectory(from)) {
                if (not FileSystemManager::Rename(from, to)) {
                    Logger::Log(LogLevel::Error,
                                "Failed to rename {} to {}",
                                from.string(),
                                to.string());
                    return false;
                }
            }
        }
    }

    // Finally, with a shared lock, clean up the directory to be removed
    {
        auto lock = SharedLock(storage_config);
        if (not lock) {
            Logger::Log(LogLevel::Error,
                        "Failed to get a shared lock for the repository root");
            return false;
        }
        if (FileSystemManager::IsDirectory(remove_me)) {
            if (not FileSystemManager::RemoveDirectory(remove_me,
                                                       /*recursively=*/true)) {
                Logger::Log(LogLevel::Error,
                            "Failed to remove directory {}",
                            remove_me.string());
                return false;
            }
        }
    }

    return true;
}
