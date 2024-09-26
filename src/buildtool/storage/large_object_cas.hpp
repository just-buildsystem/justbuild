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

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/uplinker.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

template <bool>
class LocalCAS;

enum class LargeObjectErrorCode : std::uint8_t {
    /// \brief An internal error occured.
    Internal = 0,

    /// \brief The digest is not in the CAS.
    FileNotFound,

    /// \brief The result is different from what was expected.
    InvalidResult,

    /// \brief Some parts of the tree are not in the storage.
    InvalidTree
};

/// \brief Describes an error that occurred during split-splice.
class LargeObjectError final {
  public:
    LargeObjectError(LargeObjectErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    /// \brief Obtain the error code.
    [[nodiscard]] auto Code() const noexcept -> LargeObjectErrorCode {
        return code_;
    }

    /// \brief Obtain the error message.
    [[nodiscard]] auto Message() && noexcept -> std::string {
        return std::move(message_);
    }

  private:
    LargeObjectErrorCode const code_;
    std::string message_;
};

/// \brief Stores a temporary directory containing a result of splicing.
class LargeObject final {
  public:
    explicit LargeObject(StorageConfig const& storage_config) noexcept
        : directory_(storage_config.CreateTypedTmpDir("splice")),
          path_(directory_ ? directory_->GetPath() / "result" : ".") {}

    /// \brief Check whether the large object is valid.
    [[nodiscard]] auto IsValid() const noexcept -> bool {
        return directory_ != nullptr;
    }

    /// \brief Obtain the path to the spliced result.
    [[nodiscard]] auto GetPath() const noexcept
        -> std::filesystem::path const& {
        return path_;
    }

  private:
    TmpDirPtr directory_;
    std::filesystem::path path_;
};

/// \brief Stores auxiliary information for reconstructing large objects.
/// The entries are keyed by the hash of the spliced result and the value of an
/// entry is the concatenation of the hashes of chunks the large object is
/// composed of.
template <bool kDoGlobalUplink, ObjectType kType>
class LargeObjectCAS final {
  public:
    explicit LargeObjectCAS(
        gsl::not_null<LocalCAS<kDoGlobalUplink> const*> const& local_cas,
        GenerationConfig const& config,
        gsl::not_null<Uplinker<kDoGlobalUplink> const*> const&
            uplinker) noexcept
        : local_cas_(*local_cas),
          storage_config_{*config.storage_config},
          uplinker_{*uplinker},
          file_store_(IsTreeObject(kType) ? config.cas_large_t
                                          : config.cas_large_f) {}

    LargeObjectCAS(LargeObjectCAS const&) = delete;
    LargeObjectCAS(LargeObjectCAS&&) = delete;
    auto operator=(LargeObjectCAS const&) -> LargeObjectCAS& = delete;
    auto operator=(LargeObjectCAS&&) -> LargeObjectCAS& = delete;
    ~LargeObjectCAS() noexcept = default;

    /// \brief Obtain path to the storage root.
    [[nodiscard]] auto StorageRoot() const noexcept
        -> std::filesystem::path const& {
        return file_store_.StorageRoot();
    }

    /// \brief Get the path to a large entry in the storage.
    /// \param  digest      The digest of a large object.
    /// \returns            Path to the large entry if in the storage.
    [[nodiscard]] auto GetEntryPath(ArtifactDigest const& digest) const noexcept
        -> std::optional<std::filesystem::path>;

    /// \brief Split an object from the main CAS into chunks. If the object had
    /// been split before, it would not get split again.
    /// \param digest       The digest of the object to be split.
    /// \return             A set of chunks the resulting object is composed of
    /// or an error on failure.
    [[nodiscard]] auto Split(ArtifactDigest const& digest) const noexcept
        -> expected<std::vector<ArtifactDigest>, LargeObjectError>;

    /// \brief Splice an object based on the reconstruction rules from the
    /// storage. This method doesn't check whether the result of splicing is
    /// already in the CAS.
    /// \param digest       The digest of the object to be spliced.
    /// \return             A temporary directory that contains a single file
    /// "result" on success or an error on failure.
    [[nodiscard]] auto TrySplice(ArtifactDigest const& digest) const noexcept
        -> expected<LargeObject, LargeObjectError>;

    /// \brief Splice an object from parts. This method doesn't check whether
    /// the result of splicing is already in the CAS.
    /// \param digest       The digest of the resulting object.
    /// \param parts        Parts to be concatenated.
    /// \return             A temporary directory that contains a single file
    /// "result" on success or an error on failure.
    [[nodiscard]] auto Splice(ArtifactDigest const& digest,
                              std::vector<ArtifactDigest> const& parts)
        const noexcept -> expected<LargeObject, LargeObjectError>;

    /// \brief Uplink large entry from this generation to latest LocalCAS
    /// generation. For the large entry it's parts get promoted first and then
    /// the entry itself. This function is only available for instances that are
    /// used as local GC generations (i.e., disabled global uplink).
    /// \tparam kIsLocalGeneration  True if this instance is a local generation.
    /// \param latest       The latest LocalCAS generation.
    /// \param latest_large The latest LargeObjectCAS
    /// \param digest       The digest of the large entry to uplink.
    /// \returns True if the large entry was successfully uplinked.
    template <bool kIsLocalGeneration = not kDoGlobalUplink>
        requires(kIsLocalGeneration)
    [[nodiscard]] auto LocalUplink(
        LocalCAS<false> const& latest,
        LargeObjectCAS<false, kType> const& latest_large,
        ArtifactDigest const& digest) const noexcept -> bool;

  private:
    // By default, overwrite existing entries. Unless this is a generation
    // (disabled global uplink), then we never want to overwrite any entries.
    static constexpr auto kStoreMode =
        kDoGlobalUplink ? StoreMode::LastWins : StoreMode::FirstWins;

    LocalCAS<kDoGlobalUplink> const& local_cas_;
    StorageConfig const& storage_config_;
    Uplinker<kDoGlobalUplink> const& uplinker_;
    FileStorage<ObjectType::File, kStoreMode, /*kSetEpochTime=*/false>
        file_store_;

    /// \brief Obtain the information for reconstructing a large object.
    /// \param  digest      The digest of a large object.
    /// \returns            Parts the large object is composed of, if present in
    /// the storage.
    [[nodiscard]] auto ReadEntry(ArtifactDigest const& digest) const noexcept
        -> std::optional<std::vector<ArtifactDigest>>;

    /// \brief Create a new entry description and add it to the storage.
    /// \param digest       The digest of the result.
    /// \param parts        Parts the resulting object is composed of.
    /// \returns            True if the entry exists afterwards.
    [[nodiscard]] auto WriteEntry(
        ArtifactDigest const& digest,
        std::vector<ArtifactDigest> const& parts) const noexcept -> bool;
};

#include "src/buildtool/storage/large_object_cas.tpp"

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_HPP
