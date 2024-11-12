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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_GARBAGE_COLLECTOR_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_GARBAGE_COLLECTOR_HPP

#include <cstddef>
#include <filesystem>
#include <optional>

#include "src/buildtool/storage/config.hpp"
#include "src/utils/cpp/file_locking.hpp"

/// \brief Global garbage collector implementation.
/// Responsible for deleting oldest generation.
class GarbageCollector {
  public:
    /// \brief Trigger garbage collection; unless no_rotation is given, this
    /// will include rotation of generations and deleting the oldest generation.
    /// \returns true on success.
    [[nodiscard]] auto static TriggerGarbageCollection(
        StorageConfig const& storage_config,
        bool no_rotation = false) noexcept -> bool;

    /// \brief Acquire shared lock to prevent garbage collection from running.
    /// \param storage_config   Storage to be locked.
    /// \returns The acquired lock file on success or nullopt otherwise.
    [[nodiscard]] auto static SharedLock(
        StorageConfig const& storage_config) noexcept
        -> std::optional<LockFile>;

  private:
    [[nodiscard]] auto static ExclusiveLock(
        StorageConfig const& storage_config) noexcept
        -> std::optional<LockFile>;

    [[nodiscard]] auto static LockFilePath(
        StorageConfig const& storage_config) noexcept -> std::filesystem::path;

    /// \brief Remove spliced objects from the youngest generation and split
    /// objects that are larger than the threshold.
    /// \param threshold    Compactification threshold.
    /// \return True if the youngest generation does not contain splicable
    /// objects afterwards.
    [[nodiscard]] auto static Compactify(StorageConfig const& storage_config,
                                         size_t threshold) noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_GARBAGE_COLLECTOR_HPP
