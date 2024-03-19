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

#include <functional>
#include <optional>
#include <string>

#include "src/utils/cpp/file_locking.hpp"

// forward declarations
namespace build::bazel::remote::execution::v2 {
class Digest;
}
namespace bazel_re = build::bazel::remote::execution::v2;
class TargetCacheKey;

/// \brief Global garbage collector implementation.
/// Responsible for deleting oldest generation and uplinking across all
/// generations to latest generation.
class GarbageCollector {
  public:
    /// \brief Uplink blob across LocalCASes from all generations to latest.
    /// Note that blobs will NOT be synced between file/executable CAS.
    /// \param digest           Digest of the blob to uplink.
    /// \param is_executable    Indicate that blob is an executable.
    /// \returns true if blob was found and successfully uplinked.
    [[nodiscard]] auto static GlobalUplinkBlob(bazel_re::Digest const& digest,
                                               bool is_executable) noexcept
        -> bool;

    /// \brief Uplink tree across LocalCASes from all generations to latest.
    /// Note that the tree will be deeply uplinked, i.e., all entries referenced
    /// by this tree will be uplinked before (including sub-trees).
    /// \param digest   Digest of the tree to uplink.
    /// \returns true if tree was found and successfully uplinked (deep).
    [[nodiscard]] auto static GlobalUplinkTree(
        bazel_re::Digest const& digest) noexcept -> bool;

    /// \brief Uplink entry from action cache across all generations to latest.
    /// Note that the entry will be uplinked including all referenced items.
    /// \param action_id    Id of the action to uplink entry for.
    /// \returns true if cache entry was found and successfully uplinked.
    [[nodiscard]] auto static GlobalUplinkActionCacheEntry(
        bazel_re::Digest const& action_id) noexcept -> bool;

    /// \brief Uplink entry from target cache across all generations to latest.
    /// Note that the entry will be uplinked including all referenced items.
    /// \param key  Target cache key to uplink entry for.
    /// \returns true if cache entry was found and successfully uplinked.
    [[nodiscard]] auto static GlobalUplinkTargetCacheEntry(
        TargetCacheKey const& key) noexcept -> bool;

    /// \brief Trigger garbage collection; unless no_rotation is given, this
    /// will include rotation of generations and deleting the oldest generation.
    /// \returns true on success.
    [[nodiscard]] auto static TriggerGarbageCollection(
        bool no_rotation = false) noexcept -> bool;

    /// \brief Acquire shared lock to prevent garbage collection from running.
    /// \returns The acquired lock file on success or nullopt otherwise.
    [[nodiscard]] auto static SharedLock() noexcept -> std::optional<LockFile>;

  private:
    [[nodiscard]] auto static ExclusiveLock() noexcept
        -> std::optional<LockFile>;

    [[nodiscard]] auto static LockFilePath() noexcept -> std::filesystem::path;
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_GARBAGE_COLLECTOR_HPP
