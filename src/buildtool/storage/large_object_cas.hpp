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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_HPP

#include <filesystem>
#include <optional>
#include <vector>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/object_type.hpp"

/// \brief Stores auxiliary information for reconstructing large objects.
/// The entries are keyed by the hash of the spliced result and the value of an
/// entry is the concatenation of the hashes of chunks the large object is
/// composed of.
template <bool kDoGlobalUplink>
class LargeObjectCAS final {
  public:
    explicit LargeObjectCAS(std::filesystem::path const& store_path) noexcept
        : file_store_(store_path) {}

    LargeObjectCAS(LargeObjectCAS const&) = delete;
    LargeObjectCAS(LargeObjectCAS&&) = delete;
    auto operator=(LargeObjectCAS const&) -> LargeObjectCAS& = delete;
    auto operator=(LargeObjectCAS&&) -> LargeObjectCAS& = delete;
    ~LargeObjectCAS() noexcept = default;

    /// \brief Get the path to a large entry in the storage.
    /// \param  digest      The digest of a large object.
    /// \returns            Path to the large entry if in the storage.
    [[nodiscard]] auto GetEntryPath(bazel_re::Digest const& digest)
        const noexcept -> std::optional<std::filesystem::path>;

  private:
    // By default, overwrite existing entries. Unless this is a generation
    // (disabled global uplink), then we never want to overwrite any entries.
    static constexpr auto kStoreMode =
        kDoGlobalUplink ? StoreMode::LastWins : StoreMode::FirstWins;

    FileStorage<ObjectType::File, kStoreMode, /*kSetEpochTime=*/false>
        file_store_;

    /// \brief Obtain the information for reconstructing a large object.
    /// \param  digest      The digest of a large object.
    /// \returns            Parts the large object is composed of, if present in
    /// the storage.
    [[nodiscard]] auto ReadEntry(bazel_re::Digest const& digest) const noexcept
        -> std::optional<std::vector<bazel_re::Digest>>;

    /// \brief Create a new entry description and add it to the storage.
    /// \param digest       The digest of the result.
    /// \param parts        Parts the resulting object is composed of.
    /// \returns            True if the entry exists afterwards.
    [[nodiscard]] auto WriteEntry(
        bazel_re::Digest const& digest,
        std::vector<bazel_re::Digest> const& parts) const noexcept -> bool;
};

#include "src/buildtool/storage/large_object_cas.tpp"

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_HPP
