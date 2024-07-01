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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_UPLINKER_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_UPLINKER_HPP

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/storage/config.hpp"

template <bool>
class LocalStorage;
class TargetCacheKey;

namespace build::bazel::remote::execution::v2 {
class Digest;
}
namespace bazel_re = build::bazel::remote::execution::v2;

/// \brief Global uplinker implementation.
/// Responsible for uplinking objects across all generations to latest
/// generation.
class GlobalUplinker final {
  public:
    explicit GlobalUplinker(
        gsl::not_null<StorageConfig const*> const& storage_config) noexcept;

    /// \brief Uplink blob across LocalCASes from all generations to latest.
    /// Note that blobs will NOT be synced between file/executable CAS.
    /// \param digest         Digest of the blob to uplink.
    /// \param is_executable  Indicate that blob is an executable.
    /// \returns true if blob was found and successfully uplinked.
    [[nodiscard]] auto UplinkBlob(bazel_re::Digest const& digest,
                                  bool is_executable) const noexcept -> bool;

    /// \brief Uplink tree across LocalCASes from all generations to latest.
    /// Note that the tree will be deeply uplinked, i.e., all entries referenced
    /// by this tree will be uplinked before (including sub-trees).
    /// \param digest         Digest of the tree to uplink.
    /// \returns true if tree was found and successfully uplinked (deep).
    [[nodiscard]] auto UplinkTree(bazel_re::Digest const& digest) const noexcept
        -> bool;

    /// \brief Uplink large blob entry across LocalCASes from all generations to
    /// latest. This method does not splice the large object.
    /// \param digest         Digest of the large blob entry to uplink.
    /// \returns true if large entry was found and successfully uplinked.
    [[nodiscard]] auto UplinkLargeBlob(
        bazel_re::Digest const& digest) const noexcept -> bool;

    /// \brief Uplink entry from action cache across all generations to latest.
    /// Note that the entry will be uplinked including all referenced items.
    /// \param action_id    Id of the action to uplink entry for.
    /// \returns true if cache entry was found and successfully uplinked.
    [[nodiscard]] auto UplinkActionCacheEntry(
        bazel_re::Digest const& action_id) const noexcept -> bool;

    /// \brief Uplink entry from target cache across all generations to latest.
    /// Note that the entry will be uplinked including all referenced items.
    /// \param key  Target cache key to uplink entry for.
    /// \param shard Optional explicit shard, if the default is not intended.
    /// \returns true if cache entry was found and successfully uplinked.
    [[nodiscard]] auto UplinkTargetCacheEntry(
        TargetCacheKey const& key,
        std::optional<std::string> const& shard = std::nullopt) const noexcept
        -> bool;

  private:
    StorageConfig const& storage_config_;
    std::vector<LocalStorage<false>> const generations_;
};

/// \brief An empty constructable Uplinker. Although it doesn't have any
/// interface, it allows objects employing uplinking store the uplinker by
/// reference instead of unobvious 'optional' raw pointers.
class StubUplinker final {
  public:
    explicit StubUplinker(
        gsl::not_null<StorageConfig const*> const& /*unused*/) noexcept {}
};

template <bool kDoGlobalUplink>
using Uplinker =
    std::conditional_t<kDoGlobalUplink, GlobalUplinker, StubUplinker>;

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_UPLINKER_HPP
