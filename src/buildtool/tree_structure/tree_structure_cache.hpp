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

#ifndef INCLUDED_SRC_BUILDTOOL_TREE_STRUCTURE_TREE_STRUCTURE_CACHE_HPP
#define INCLUDED_SRC_BUILDTOOL_TREE_STRUCTURE_TREE_STRUCTURE_CACHE_HPP

#include <cstddef>
#include <optional>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"

class TreeStructureCache final {
  public:
    explicit TreeStructureCache(
        gsl::not_null<StorageConfig const*> const& storage_config) noexcept;

    /// \brief Obtain the digest describing the tree structure of a key tree.
    /// Can trigger deep uplinking of referenced objects (both key and value).
    [[nodiscard]] auto Get(ArtifactDigest const& key) const noexcept
        -> std::optional<ArtifactDigest>;

    /// \brief Set coupling between key and value digest signalizing that the
    /// value digest contains the tree structure of a key digest. Both key and
    /// value are expected to be in the storage. Can trigger deep uplinking of
    /// objects (both key and value).
    /// \return True if the cache contains the key-value coupling. Fails if
    /// there is the key in the storage, but it refers to another value. Fails
    /// if key or value aren't present in the storage.
    [[nodiscard]] auto Set(ArtifactDigest const& key,
                           ArtifactDigest const& value) const noexcept -> bool;

  private:
    StorageConfig const& storage_config_;
    FileStorage<ObjectType::File, StoreMode::FirstWins, false> file_storage_;
    bool uplink_;

    explicit TreeStructureCache(
        gsl::not_null<StorageConfig const*> const& storage_config,
        std::size_t generation,
        bool uplink) noexcept;

    [[nodiscard]] auto LocalUplinkObject(
        ArtifactDigest const& key,
        TreeStructureCache const& latest) const noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_TREE_STRUCTURE_TREE_STRUCTURE_CACHE_HPP
