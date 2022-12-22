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

#include "src/buildtool/execution_api/local/garbage_collector.hpp"

#include <vector>

#include <nlohmann/json.hpp>

#include "src/buildtool/build_engine/target_map/target_cache_entry.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

auto GarbageCollector::FindAndUplinkBlob(std::string const& id,
                                         bool is_executable) noexcept -> bool {
    // Try to find blob in all generations.
    for (int i = 0; i < LocalExecutionConfig::NumGenerations(); i++) {
        if (UplinkBlob(i, id, is_executable)) {
            return true;
        }
    }
    return false;
}

auto GarbageCollector::FindAndUplinkTree(std::string const& id) noexcept
    -> bool {
    // Try to find tree in all generations.
    for (int i = 0; i < LocalExecutionConfig::NumGenerations(); i++) {
        if (UplinkTree(i, id)) {
            return true;
        }
    }
    return false;
}

auto GarbageCollector::FindAndUplinkActionCacheEntry(
    std::string const& id) noexcept -> bool {
    // Try to find action-cache entry in all generations.
    for (int i = 0; i < LocalExecutionConfig::NumGenerations(); i++) {
        if (UplinkActionCacheEntry(i, id)) {
            return true;
        }
    }
    return false;
}

auto GarbageCollector::FindAndUplinkTargetCacheEntry(
    std::string const& id) noexcept -> bool {
    // Try to find target-cache entry in all generations.
    for (int i = 0; i < LocalExecutionConfig::NumGenerations(); i++) {
        if (UplinkTargetCacheEntry(i, id)) {
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
    return LocalExecutionConfig::CacheRoot() / "gc.lock";
}

auto GarbageCollector::TriggerGarbageCollection() noexcept -> bool {
    auto pid = CreateProcessUniqueId();
    if (not pid) {
        return false;
    }
    auto remove_me = std::string{"remove-me-"} + *pid;
    auto remove_me_dir = LocalExecutionConfig::CacheRoot() / remove_me;
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
        for (int i = LocalExecutionConfig::NumGenerations() - 1; i >= 0; i--) {
            auto cache_root = LocalExecutionConfig::CacheRoot(i);
            if (FileSystemManager::IsDirectory(cache_root)) {
                auto new_cache_root =
                    (i == LocalExecutionConfig::NumGenerations() - 1)
                        ? remove_me_dir
                        : LocalExecutionConfig::CacheRoot(i + 1);
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

auto GarbageCollector::UplinkBlob(int index,
                                  std::string const& id,
                                  bool is_executable) noexcept -> bool {
    // Determine blob path of given generation.
    auto root =
        is_executable
            ? LocalExecutionConfig::CASDir<ObjectType::Executable>(index)
            : LocalExecutionConfig::CASDir<ObjectType::File>(index);
    auto blob_path = GetStoragePath(root, id);
    if (not FileSystemManager::IsFile(blob_path)) {
        return false;
    }

    // Determine blob path in latest generation.
    auto root_latest =
        is_executable ? LocalExecutionConfig::CASDir<ObjectType::Executable>(0)
                      : LocalExecutionConfig::CASDir<ObjectType::File>(0);
    auto blob_path_latest = GetStoragePath(root_latest, id);
    if (not FileSystemManager::IsFile(blob_path_latest)) {
        // Uplink blob from older generation to the latest generation.
        if (not FileSystemManager::CreateDirectory(
                blob_path_latest.parent_path())) {
            return false;
        }
        if (not FileSystemManager::CreateFileHardlink(
                blob_path,
                blob_path_latest,
                /*log_failure_at=*/LogLevel::Debug) and
            not FileSystemManager::IsFile(blob_path_latest)) {
            return false;
        }
    }
    return true;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto GarbageCollector::UplinkTree(int index, std::string const& id) noexcept
    -> bool {
    // Determine tree path of given generation.
    auto root = LocalExecutionConfig::CASDir<ObjectType::Tree>(index);
    auto tree_path = GetStoragePath(root, id);
    if (not FileSystemManager::IsFile(tree_path)) {
        return false;
    }

    // Determine tree path in latest generation.
    auto root_latest = LocalExecutionConfig::CASDir<ObjectType::Tree>(0);
    auto tree_path_latest = GetStoragePath(root_latest, id);
    if (not FileSystemManager::IsFile(tree_path_latest)) {
        // Determine tree entries.
        auto content = FileSystemManager::ReadFile(tree_path);
        auto tree_entries = GitRepo::ReadTreeData(*content,
                                                  id,
                                                  /*is_hex_id=*/true);
        if (not tree_entries) {
            return false;
        }

        // Uplink tree entries.
        for (auto const& [raw_id, entry_vector] : *tree_entries) {
            // Process only first entry from 'entry_vector' since all entries
            // represent the same blob, just with different names.
            auto entry = entry_vector.front();
            auto hash = ToHexString(raw_id);
            if (entry.type == ObjectType::Tree) {
                if (not UplinkTree(index, hash)) {
                    return false;
                }
            }
            else {
                if (not UplinkBlob(
                        index, hash, entry.type == ObjectType::Executable)) {
                    return false;
                }
            }
        }

        // Uplink tree from older generation to the latest generation.
        if (not FileSystemManager::CreateDirectory(
                tree_path_latest.parent_path())) {
            return false;
        }
        if (not FileSystemManager::CreateFileHardlink(
                tree_path,
                tree_path_latest,
                /*log_failure_at=*/LogLevel::Debug) and
            not FileSystemManager::IsFile(tree_path_latest)) {
            return false;
        }
    }
    return true;
}

auto GarbageCollector::UplinkBazelTree(int index,
                                       std::string const& id) noexcept -> bool {
    // Determine bazel tree path of given generation.
    auto root = LocalExecutionConfig::CASDir<ObjectType::File>(index);
    auto tree_path = GetStoragePath(root, id);
    if (not FileSystemManager::IsFile(tree_path)) {
        return false;
    }

    // Determine bazel tree entries.
    auto content = FileSystemManager::ReadFile(tree_path);
    bazel_re::Tree tree{};
    if (not tree.ParseFromString(*content)) {
        return false;
    }

    // Uplink bazel tree entries.
    auto dir = tree.root();
    for (auto const& file : dir.files()) {
        if (not UplinkBlob(index,
                           NativeSupport::Unprefix(file.digest().hash()),
                           file.is_executable())) {
            return false;
        }
    }
    for (auto const& directory : dir.directories()) {
        if (not UplinkBazelDirectory(
                index, NativeSupport::Unprefix(directory.digest().hash()))) {
            return false;
        }
    }

    // Determine bazel tree path in latest generation.
    auto root_latest = LocalExecutionConfig::CASDir<ObjectType::File>(0);
    auto tree_path_latest = GetStoragePath(root_latest, id);

    // Uplink bazel tree from older generation to the latest generation.
    if (not FileSystemManager::IsFile(tree_path_latest)) {
        if (not FileSystemManager::CreateDirectory(
                tree_path_latest.parent_path())) {
            return false;
        }
        if (not FileSystemManager::CreateFileHardlink(
                tree_path,
                tree_path_latest,
                /*log_failure_at=*/LogLevel::Debug) and
            not FileSystemManager::IsFile(tree_path_latest)) {
            return false;
        }
    }
    return true;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto GarbageCollector::UplinkBazelDirectory(int index,
                                            std::string const& id) noexcept
    -> bool {
    // Determine bazel directory path of given generation.
    auto root = LocalExecutionConfig::CASDir<ObjectType::File>(index);
    auto dir_path = GetStoragePath(root, id);
    if (not FileSystemManager::IsFile(dir_path)) {
        return false;
    }

    // Determine bazel directory entries.
    auto content = FileSystemManager::ReadFile(dir_path);
    bazel_re::Directory dir{};
    if (not dir.ParseFromString(*content)) {
        return false;
    }

    // Uplink bazel directory entries.
    for (auto const& file : dir.files()) {
        if (not UplinkBlob(index,
                           NativeSupport::Unprefix(file.digest().hash()),
                           file.is_executable())) {
            return false;
        }
    }
    for (auto const& directory : dir.directories()) {
        if (not UplinkBazelDirectory(
                index, NativeSupport::Unprefix(directory.digest().hash()))) {
            return false;
        }
    }

    // Determine bazel directory path in latest generation.
    auto root_latest = LocalExecutionConfig::CASDir<ObjectType::File>(0);
    auto dir_path_latest = GetStoragePath(root_latest, id);

    // Uplink bazel directory from older generation to the latest generation.
    if (not FileSystemManager::IsFile(dir_path_latest)) {
        if (not FileSystemManager::CreateDirectory(
                dir_path_latest.parent_path())) {
            return false;
        }
        if (not FileSystemManager::CreateFileHardlink(
                dir_path,
                dir_path_latest,
                /*log_failure_at=*/LogLevel::Debug) and
            not FileSystemManager::IsFile(dir_path_latest)) {
            return false;
        }
    }
    return true;
}

auto GarbageCollector::UplinkActionCacheEntry(int index,
                                              std::string const& id) noexcept
    -> bool {
    // Determine action-cache entry path of given generation.
    auto root = LocalExecutionConfig::ActionCacheDir(index);
    auto entry_path = GetStoragePath(root, id);
    if (not FileSystemManager::IsFile(entry_path)) {
        return false;
    }

    // Determine action-cache entry location.
    auto content = FileSystemManager::ReadFile(entry_path, ObjectType::File);
    bazel_re::Digest digest{};
    if (not digest.ParseFromString(*content)) {
        return false;
    }

    // Uplink action-cache entry blob.
    if (not UplinkActionCacheEntryBlob(
            index, NativeSupport::Unprefix(digest.hash()))) {
        return false;
    }

    // Determine action-cache entry path in latest generation.
    auto root_latest = LocalExecutionConfig::ActionCacheDir(0);
    auto entry_path_latest = GetStoragePath(root_latest, id);

    // Uplink action-cache entry from older generation to the latest
    // generation.
    if (not FileSystemManager::IsFile(entry_path_latest)) {
        if (not FileSystemManager::CreateDirectory(
                entry_path_latest.parent_path())) {
            return false;
        }
        if (not FileSystemManager::CreateFileHardlink(
                entry_path,
                entry_path_latest,
                /*log_failure_at=*/LogLevel::Debug) and
            not FileSystemManager::IsFile(entry_path_latest)) {
            return false;
        }
    }
    return true;
}

auto GarbageCollector::UplinkActionCacheEntryBlob(
    int index,
    std::string const& id) noexcept -> bool {

    // Determine action-cache entry blob path of given generation.
    auto root = LocalExecutionConfig::CASDir<ObjectType::File>(index);
    auto entry_path = GetStoragePath(root, id);
    if (not FileSystemManager::IsFile(entry_path)) {
        return false;
    }

    // Determine artifacts referenced by action-cache entry.
    auto content = FileSystemManager::ReadFile(entry_path);
    bazel_re::ActionResult result{};
    if (not result.ParseFromString(*content)) {
        return false;
    }

    // Uplink referenced artifacts.
    for (auto const& file : result.output_files()) {
        if (not UplinkBlob(index,
                           NativeSupport::Unprefix(file.digest().hash()),
                           file.is_executable())) {
            return false;
        }
    }
    for (auto const& directory : result.output_directories()) {
        if (Compatibility::IsCompatible()) {
            if (not UplinkBazelTree(
                    index,
                    NativeSupport::Unprefix(directory.tree_digest().hash()))) {
                return false;
            }
        }
        else {
            if (not UplinkTree(
                    index,
                    NativeSupport::Unprefix(directory.tree_digest().hash()))) {
                return false;
            }
        }
    }

    // Determine action-cache entry blob path in latest generation.
    auto root_latest = LocalExecutionConfig::CASDir<ObjectType::File>(0);
    auto entry_path_latest = GetStoragePath(root_latest, id);

    // Uplink action-cache entry blob from older generation to the latest
    // generation.
    if (not FileSystemManager::IsFile(entry_path_latest)) {
        if (not FileSystemManager::CreateDirectory(
                entry_path_latest.parent_path())) {
            return false;
        }
        if (not FileSystemManager::CreateFileHardlink(
                entry_path,
                entry_path_latest,
                /*log_failure_at=*/LogLevel::Debug) and
            not FileSystemManager::IsFile(entry_path_latest)) {
            return false;
        }
    }
    return true;
}

auto GarbageCollector::UplinkTargetCacheEntry(int index,
                                              std::string const& id) noexcept
    -> bool {

    // Determine target-cache entry path of given generation.
    auto root = LocalExecutionConfig::TargetCacheDir(index);
    auto entry_path = GetStoragePath(root, id);
    if (not FileSystemManager::IsFile(entry_path)) {
        return false;
    }

    // Determine target-cache entry location.
    auto content = FileSystemManager::ReadFile(entry_path);
    auto info = Artifact::ObjectInfo::FromString(*content);
    if (not info) {
        return false;
    }

    // Uplink target-cache entry blob.
    if (not UplinkTargetCacheEntryBlob(index, info->digest.hash())) {
        return false;
    }

    // Determine target-cache entry path in latest generation.
    auto root_latest = LocalExecutionConfig::TargetCacheDir(0);
    auto entry_path_latest = GetStoragePath(root_latest, id);

    // Uplink target-cache entry from older generation to the latest
    // generation.
    if (not FileSystemManager::IsFile(entry_path_latest)) {
        if (not FileSystemManager::CreateDirectory(
                entry_path_latest.parent_path())) {
            return false;
        }
        if (not FileSystemManager::CreateFileHardlink(
                entry_path,
                entry_path_latest,
                /*log_failure_at=*/LogLevel::Debug) and
            not FileSystemManager::IsFile(entry_path_latest)) {
            return false;
        }
    }
    return true;
}

auto GarbageCollector::UplinkTargetCacheEntryBlob(
    int index,
    std::string const& id) noexcept -> bool {

    // Determine target-cache entry blob path of given generation.
    auto root = LocalExecutionConfig::CASDir<ObjectType::File>(index);
    auto entry_path = GetStoragePath(root, id);
    if (not FileSystemManager::IsFile(entry_path)) {
        return false;
    }

    // Determine artifacts referenced by target-cache entry.
    auto content = FileSystemManager::ReadFile(entry_path);
    nlohmann::json json_desc{};
    try {
        json_desc = nlohmann::json::parse(*content);
    } catch (std::exception const& ex) {
        return false;
    }
    auto entry = TargetCacheEntry::FromJson(json_desc);
    std::vector<Artifact::ObjectInfo> artifacts_info;
    if (not entry.ToArtifacts(&artifacts_info)) {
        return false;
    }

    // Uplink referenced artifacts.
    for (auto const& info : artifacts_info) {
        auto hash = info.digest.hash();
        if (info.type == ObjectType::Tree) {
            if (Compatibility::IsCompatible()) {
                if (not UplinkBazelDirectory(index, hash)) {
                    return false;
                }
            }
            else {
                if (not UplinkTree(index, hash)) {
                    return false;
                }
            }
        }
        else {
            if (not UplinkBlob(
                    index, hash, info.type == ObjectType::Executable)) {
                return false;
            }
        }
    }

    // Determine target-cache entry blob path in latest generation.
    auto root_latest = LocalExecutionConfig::CASDir<ObjectType::File>(0);
    auto entry_path_latest = GetStoragePath(root_latest, id);

    // Uplink target-cache entry blob from older generation to the latest
    // generation.
    if (not FileSystemManager::IsFile(entry_path_latest)) {
        if (not FileSystemManager::CreateDirectory(
                entry_path_latest.parent_path())) {
            return false;
        }
        if (not FileSystemManager::CreateFileHardlink(
                entry_path,
                entry_path_latest,
                /*log_failure_at=*/LogLevel::Debug) and
            not FileSystemManager::IsFile(entry_path_latest)) {
            return false;
        }
    }
    return true;
}
