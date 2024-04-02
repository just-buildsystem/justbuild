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

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/local_cas.hpp"

namespace {
/// \brief Remove spliced entries from the kType storage.
/// \tparam kType         Type of the storage to inspect.
/// \param cas            Storage to be inspected.
/// \return               True if the kType storage doesn't contain spliced
/// entries.
template <ObjectType... kType>
requires(sizeof...(kType) != 0)
    [[nodiscard]] auto RemoveSpliced(LocalCAS<false> const& cas) noexcept
    -> bool;
}  // namespace

auto Compactifier::RemoveSpliced(LocalCAS<false> const& cas) noexcept -> bool {
    return ::RemoveSpliced<ObjectType::Tree>(cas) and
           ::RemoveSpliced<ObjectType::File, ObjectType::Executable>(cas);
}

namespace {
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
}  // namespace
