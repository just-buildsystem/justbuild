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

#include "src/buildtool/storage/compactifier.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/crypto/hasher.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/compactification_task.hpp"
#include "src/buildtool/storage/local_cas.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {
/// \brief Remove invalid entries from the key directory. The directory itself
/// can be removed too, if it has an invalid name.
/// A task is keyed by a two-letter directory name and the type of a storage
/// being checked.
/// \tparam kType         Type of the storage to inspect.
/// \param task           Owning compactification task.
/// \param entry          Directory entry.
/// \return               True if the entry directory doesn't contain invalid
/// entries.
template <ObjectType kType>
[[nodiscard]] auto RemoveInvalid(CompactificationTask const& task,
                                 std::filesystem::path const& key) noexcept
    -> bool;

/// \brief Remove spliced entries from the kType storage.
/// A task is keyed by a directory name concisting of two letters and kType...
/// storages need to be checked.
/// \tparam kLargeType    Type of the large storage to scan.
/// \tparam kType         Types of the storages to inspect.
/// \param task           Owning compactification task.
/// \param entry          Directory entry.
/// \return               True if the entry directory doesn't contain spliced
/// entries.
template <ObjectType kLargeType, ObjectType... kType>
requires(sizeof...(kType) != 0)
    [[nodiscard]] auto RemoveSpliced(CompactificationTask const& task,
                                     std::filesystem::path const& key) noexcept
    -> bool;

/// \brief Split and remove a key entry from the kType storage. Results of
/// splitting are added to the LocalCAS.
/// \tparam kType         Type of the storage to inspect.
/// \param task           Owning compactification task.
/// \param entry          File entry.
/// \return               True if the file was split succesfully.
template <ObjectType kType>
[[nodiscard]] auto SplitLarge(CompactificationTask const& task,
                              std::filesystem::path const& key) noexcept
    -> bool;
}  // namespace

auto Compactifier::RemoveInvalid(LocalCAS<false> const& cas) noexcept -> bool {
    auto logger = [](LogLevel level, std::string const& msg) {
        Logger::Log(
            level, "Compactification: Removal of invalid files:\n{}", msg);
    };

    // Collect directories and remove from the storage files and directories
    // having invalid names.
    // In general, the number of files in the storage is not limited, thus
    // parallelization is done using subdirectories to not run out of memory.
    CompactificationTask task{.cas = cas,
                              .large = false,
                              .logger = logger,
                              .filter = FileSystemManager::IsDirectory,
                              .f_task = ::RemoveInvalid<ObjectType::File>,
                              .x_task = ::RemoveInvalid<ObjectType::Executable>,
                              .t_task = ::RemoveInvalid<ObjectType::Tree>};
    return CompactifyConcurrently(task);
}

auto Compactifier::RemoveSpliced(LocalCAS<false> const& cas) noexcept -> bool {
    auto logger = [](LogLevel level, std::string const& msg) {
        Logger::Log(
            level, "Compactification: Removal of spliced files:\n{}", msg);
    };

    // Collect directories from large storages and remove from the storage files
    // having correponding large entries.
    // In general, the number of files in the storage is not limited, thus
    // parallelization is done using subdirectories to not run out of memory.
    CompactificationTask task{
        .cas = cas,
        .large = true,
        .logger = logger,
        .filter = FileSystemManager::IsDirectory,
        .f_task = ::RemoveSpliced<ObjectType::File,
                                  ObjectType::File,
                                  ObjectType::Executable>,
        .t_task = ::RemoveSpliced<ObjectType::Tree, ObjectType::Tree>};
    return CompactifyConcurrently(task);
}

auto Compactifier::SplitLarge(LocalCAS<false> const& cas,
                              size_t threshold) noexcept -> bool {
    auto logger = [](LogLevel level, std::string const& msg) {
        Logger::Log(level, "Compactification: Splitting:\n{}", msg);
    };

    // Collect files larger than the threshold and split them.
    // Concurrently scanning a directory and putting new entries there may cause
    // the scan to fail. To avoid that, parallelization is done using
    // files, although this may result in a run out of memory.
    CompactificationTask task{
        .cas = cas,
        .large = false,
        .logger = logger,
        .filter =
            [threshold](std::filesystem::path const& path) {
                return not FileSystemManager::IsDirectory(path) and
                       std::filesystem::file_size(path) >= threshold;
            },
        .f_task = ::SplitLarge<ObjectType::File>,
        .x_task = ::SplitLarge<ObjectType::Executable>,
        .t_task = ::SplitLarge<ObjectType::Tree>};
    return CompactifyConcurrently(task);
}

namespace {
template <ObjectType kType>
[[nodiscard]] auto RemoveInvalid(CompactificationTask const& task,
                                 std::filesystem::path const& key) noexcept
    -> bool {
    auto const directory = task.cas.StorageRoot(kType) / key;

    // Check there are entries to process:
    if (not FileSystemManager::IsDirectory(directory)) {
        return true;
    }

    // Calculate reference hash size:
    auto const kHashSize = task.cas.GetHashFunction().Hasher().GetHashLength();
    auto const kFileNameSize =
        kHashSize - FileStorageData::kDirectoryNameLength;

    // Check the directory itself is valid:
    std::string const d_name = directory.filename();
    if (d_name.size() != FileStorageData::kDirectoryNameLength or
        not FromHexString(d_name)) {
        static constexpr bool kRecursively = true;
        if (FileSystemManager::RemoveDirectory(directory, kRecursively)) {
            return true;
        }

        task.Log(LogLevel::Error,
                 "Failed to remove invalid directory {}",
                 directory.string());
        return false;
    }

    FileSystemManager::ReadDirEntryFunc callback =
        [&task, &directory, kFileNameSize](std::filesystem::path const& file,
                                           ObjectType type) -> bool {
        // Directories are unexpected in storage subdirectories
        if (IsTreeObject(type)) {
            task.Log(LogLevel::Error,
                     "There is a directory in a storage subdirectory: {}",
                     (directory / file).string());
            return false;
        }

        // Check file has a hexadecimal name of length kFileNameSize:
        std::string const f_name = file.filename();
        if (f_name.size() == kFileNameSize and FromHexString(f_name)) {
            return true;
        }
        auto const path = directory / file;
        if (not FileSystemManager::RemoveFile(path)) {
            task.Log(LogLevel::Error,
                     "Failed to remove invalid entry {}",
                     path.string());
            return false;
        }
        return true;
    };

    // Read the key storage directory:
    if (not FileSystemManager::ReadDirectory(directory, callback)) {
        task.Log(LogLevel::Error, "Failed to read {}", directory.string());
        return false;
    }
    return true;
}

template <ObjectType kLargeType, ObjectType... kType>
requires(sizeof...(kType) != 0)
    [[nodiscard]] auto RemoveSpliced(CompactificationTask const& task,
                                     std::filesystem::path const& key) noexcept
    -> bool {
    static constexpr bool kLarge = true;
    auto const directory = task.cas.StorageRoot(kLargeType, kLarge) / key;

    // Check there are entries to process:
    if (not FileSystemManager::IsDirectory(directory)) {
        return true;
    }

    // Obtain paths to the corresponding key directories in the object storages.
    std::array const storage_roots{task.cas.StorageRoot(kType) / key...};

    FileSystemManager::ReadDirEntryFunc callback =
        [&storage_roots, &task, &directory](
            std::filesystem::path const& entry_large, ObjectType type) -> bool {
        // Directories are unexpected in storage subdirectories
        if (IsTreeObject(type)) {
            task.Log(LogLevel::Error,
                     "There is a directory in a storage subdirectory: {}",
                     (directory / entry_large).string());
            return false;
        }

        // Pathes to large entries and spliced results are:
        // large_storage / entry_large
        //       storage / entry_object
        //
        // Large objects are keyed by the hash of their spliced result, so for
        // splicable objects large_entry and object_entry are the same.
        // Thus, to check the existence of the spliced result, it is
        // enough to check the existence of { storage / entry_large }:
        auto check = [&entry_large](std::filesystem::path const& storage) {
            std::filesystem::path file_path = storage / entry_large;
            return not FileSystemManager::IsFile(file_path) or
                   FileSystemManager::RemoveFile(file_path);
        };
        return std::all_of(storage_roots.begin(), storage_roots.end(), check);
    };

    // Read the key storage directory:
    if (not FileSystemManager::ReadDirectory(directory, callback)) {
        task.Log(LogLevel::Error, "Failed to read {}", directory.string());
        return false;
    }
    return true;
}

template <ObjectType kType>
[[nodiscard]] auto SplitLarge(CompactificationTask const& task,
                              std::filesystem::path const& key) noexcept
    -> bool {
    auto const path = task.cas.StorageRoot(kType) / key;
    // Check the entry exists:
    if (not FileSystemManager::IsFile(path)) {
        return true;
    }

    // Calculate the digest for the entry:
    auto const digest =
        ArtifactDigest::CreateFromFile<kType>(task.cas.GetHashFunction(), path);
    if (not digest) {
        task.Log(LogLevel::Error,
                 "Failed to calculate digest for {}",
                 path.string());
        return false;
    }

    // Split the entry:
    auto split_result = IsTreeObject(kType) ? task.cas.SplitTree(*digest)
                                            : task.cas.SplitBlob(*digest);
    if (not split_result) {
        task.Log(LogLevel::Error,
                 "Failed to split {}\nDigest: {}\nMessage: {}",
                 path.string(),
                 digest->hash(),
                 std::move(split_result).error().Message());
        return false;
    }

    // If the file cannot actually be split (the threshold is too low), the
    // file must not be deleted.
    if (split_result->size() < 2) {
        task.Log(LogLevel::Debug,
                 "{} cannot be compactified. The compactification "
                 "threshold is too low.",
                 digest->hash());
        return true;
    }

    if (not FileSystemManager::RemoveFile(path)) {
        task.Log(LogLevel::Error, "Failed to remove {}", path.string());
        return false;
    }
    return true;
}
}  // namespace
