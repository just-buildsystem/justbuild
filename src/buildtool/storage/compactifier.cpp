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
#include <optional>
#include <variant>
#include <vector>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/crypto/hasher.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_cas.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/local_cas.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {
/// \brief Remove invalid entries from the kType storage.
/// \tparam kType         Type of the storage to inspect.
/// \param cas            Storage to be inspected.
/// \return               True if the kType storage doesn't contain invalid
/// entries.
template <ObjectType kType>
[[nodiscard]] auto RemoveInvalid(LocalCAS<false> const& cas) noexcept -> bool;

/// \brief Remove spliced entries from the kType storage.
/// \tparam kType         Type of the storage to inspect.
/// \param cas            Storage to be inspected.
/// \return               True if the kType storage doesn't contain spliced
/// entries.
template <ObjectType... kType>
requires(sizeof...(kType) != 0)
    [[nodiscard]] auto RemoveSpliced(LocalCAS<false> const& cas) noexcept
    -> bool;

/// \brief Split and remove from the kType storage every entry that is larger
/// than the given threshold. Results of splitting are added to the LocalCAS.
/// \tparam kType         Type of the storage to inspect.
/// \param cas            LocalCAS to store results of splitting.
/// \param threshold      Minimum size of an entry to be split.
/// \return               True if the kType storage doesn't contain splitable
/// entries larger than the compactification threshold afterwards.
template <ObjectType kType>
[[nodiscard]] auto SplitLarge(LocalCAS<false> const& cas,
                              size_t threshold) noexcept -> bool;
}  // namespace

auto Compactifier::RemoveInvalid(LocalCAS<false> const& cas) noexcept -> bool {
    return ::RemoveInvalid<ObjectType::File>(cas) and
           ::RemoveInvalid<ObjectType::Executable>(cas) and
           ::RemoveInvalid<ObjectType::Tree>(cas);
}

auto Compactifier::RemoveSpliced(LocalCAS<false> const& cas) noexcept -> bool {
    return ::RemoveSpliced<ObjectType::Tree>(cas) and
           ::RemoveSpliced<ObjectType::File, ObjectType::Executable>(cas);
}

auto Compactifier::SplitLarge(LocalCAS<false> const& cas,
                              size_t threshold) noexcept -> bool {
    return ::SplitLarge<ObjectType::File>(cas, threshold) and
           ::SplitLarge<ObjectType::Executable>(cas, threshold) and
           ::SplitLarge<ObjectType::Tree>(cas, threshold);
}

namespace {
template <ObjectType kType>
auto RemoveInvalid(LocalCAS<false> const& cas) noexcept -> bool {
    auto storage_root = cas.StorageRoot(kType);

    // Check there are entries to process:
    if (not FileSystemManager::IsDirectory(storage_root)) {
        return true;
    }

    // Calculate reference hash size:
    auto const kHashSize = HashFunction::Hasher().GetHashLength();
    static constexpr size_t kDirNameSize = 2;
    auto const kFileNameSize = kHashSize - kDirNameSize;

    FileSystemManager::UseDirEntryFunc callback =
        [&storage_root, kFileNameSize](std::filesystem::path const& path,
                                       bool is_tree) -> bool {
        // Use all folders.
        if (is_tree) {
            return true;
        }

        std::string const f_name = path.filename();
        std::string const d_name = path.parent_path().filename();

        // A file is valid if:
        //  * it has a hexadecimal name of length kFileNameSize;
        //  * parent directory has a hexadecimal name of length kDirNameSize.
        if (f_name.size() == kFileNameSize and FromHexString(f_name) and
            d_name.size() == kDirNameSize and FromHexString(d_name)) {
            return true;
        }
        return FileSystemManager::RemoveFile(storage_root / path);
    };
    return FileSystemManager::ReadDirectoryEntriesRecursive(storage_root,
                                                            callback);
}

template <ObjectType... kType>
requires(sizeof...(kType) != 0)
    [[nodiscard]] auto RemoveSpliced(LocalCAS<false> const& cas) noexcept
    -> bool {
    // Obtain path to the large CAS:
    static constexpr std::array types{kType...};
    auto const& large_storage = cas.StorageRoot(types[0], /*large=*/true);

    // Check there are entries to process:
    if (not FileSystemManager::IsDirectory(large_storage)) {
        return true;
    }

    // Obtain paths to object storages.
    std::array const storage_roots{cas.StorageRoot(kType)...};

    FileSystemManager::UseDirEntryFunc callback =
        [&storage_roots](std::filesystem::path const& entry_large,
                         bool is_tree) -> bool {
        // Use all folders.
        if (is_tree) {
            return true;
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
    return FileSystemManager::ReadDirectoryEntriesRecursive(large_storage,
                                                            callback);
}

template <ObjectType kType>
[[nodiscard]] auto SplitLarge(LocalCAS<false> const& cas,
                              size_t threshold) noexcept -> bool {
    // Obtain path to the storage root:
    auto const& storage_root = cas.StorageRoot(kType);

    // Check there are entries to process:
    if (not FileSystemManager::IsDirectory(storage_root)) {
        return true;
    }

    FileSystemManager::UseDirEntryFunc callback =
        [&](std::filesystem::path const& entry_object, bool is_tree) -> bool {
        // Use all folders:
        if (is_tree) {
            return true;
        }

        // Filter files by size:
        auto const path = storage_root / entry_object;
        if (std::filesystem::file_size(path) < threshold) {
            return true;
        }

        // Calculate the digest for the entry:
        auto const digest = ObjectCAS<kType>::CreateDigest(path);
        if (not digest) {
            Logger::Log(LogLevel::Error,
                        "Failed to calculate digest for {}",
                        path.generic_string());
            return false;
        }

        // Split the entry:
        auto split_result = IsTreeObject(kType) ? cas.SplitTree(*digest)
                                                : cas.SplitBlob(*digest);
        auto* parts = std::get_if<std::vector<bazel_re::Digest>>(&split_result);
        if (parts == nullptr) {
            Logger::Log(LogLevel::Error, "Failed to split {}", digest->hash());
            return false;
        }

        // If the file cannot actually be split (the threshold is too low), the
        // file must not be deleted.
        if (parts->size() < 2) {
            Logger::Log(LogLevel::Debug,
                        "{} cannot be compactified. The compactification "
                        "threshold is too low.",
                        digest->hash());
            return true;
        }
        return FileSystemManager::RemoveFile(path);
    };
    return FileSystemManager::ReadDirectoryEntriesRecursive(storage_root,
                                                            callback);
}
}  // namespace
