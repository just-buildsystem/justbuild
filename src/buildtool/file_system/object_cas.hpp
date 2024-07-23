// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_CAS_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>  // std::move

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief CAS for storing objects as plain blobs.
/// Automatically computes the digest for storing a blob from file path/bytes.
/// The actual object type is given as a template parameter. Depending on the
/// object type, files written to the file system may have different properties
/// (e.g., the x-bit set) or the digest may be computed differently (e.g., tree
/// digests in non-compatible mode). Supports custom "exists callback", which
/// is used to check blob existence before every read and write operation.
/// \tparam kType   The object type to store as blob.
template <ObjectType kType>
class ObjectCAS {
  public:
    /// \brief Callback type for checking blob existence.
    /// \returns true if a blob for the given digest exists at the given path.
    using ExistsFunc = std::function<bool(ArtifactDigest const&,
                                          std::filesystem::path const&)>;

    /// \brief Create new object CAS in store_path directory.
    /// The optional "exists callback" is used to check blob existence before
    /// every read and write operation. It promises that a blob for the given
    /// digest exists at the given path if true was returned.
    /// \param store_path   The path to use for storing blobs.
    /// \param exists       (optional) Function for checking blob existence.
    explicit ObjectCAS(
        HashFunction hash_function,
        std::filesystem::path const& store_path,
        std::optional<gsl::not_null<ExistsFunc>> exists = std::nullopt)
        : file_store_{store_path},
          exists_{exists.has_value() ? std::move(exists)->get()
                                     : kDefaultExists},
          hash_function_{hash_function} {}

    ObjectCAS(ObjectCAS const&) = delete;
    ObjectCAS(ObjectCAS&&) = delete;
    auto operator=(ObjectCAS const&) -> ObjectCAS& = delete;
    auto operator=(ObjectCAS&&) -> ObjectCAS& = delete;
    ~ObjectCAS() noexcept = default;

    /// \brief Obtain path to the storage root.
    [[nodiscard]] auto StorageRoot() const noexcept
        -> std::filesystem::path const& {
        return file_store_.StorageRoot();
    }

    /// \brief Store blob from bytes.
    /// \param bytes    The bytes do create the blob from.
    /// \returns Digest of the stored blob or nullopt in case of error.
    [[nodiscard]] auto StoreBlobFromBytes(std::string const& bytes)
        const noexcept -> std::optional<ArtifactDigest> {
        return StoreBlob(bytes, /*is_owner=*/true);
    }

    /// \brief Store blob from file path.
    /// \param file_path    The path of the file to store as blob.
    /// \param is_owner     Indicates ownership for optimization (hardlink).
    /// \returns Digest of the stored blob or nullopt in case of error.
    [[nodiscard]] auto StoreBlobFromFile(std::filesystem::path const& file_path,
                                         bool is_owner = false) const noexcept
        -> std::optional<ArtifactDigest> {
        return StoreBlob(file_path, is_owner);
    }

    /// \brief Get path to blob.
    /// \param digest   Digest of the blob to lookup.
    /// \returns Path to blob if found or nullopt otherwise.
    [[nodiscard]] auto BlobPath(ArtifactDigest const& digest) const noexcept
        -> std::optional<std::filesystem::path> {
        auto const& id = digest.hash();
        auto blob_path = file_store_.GetPath(id);
        if (not IsAvailable(digest, blob_path)) {
            logger_.Emit(LogLevel::Debug, "Blob not found {}", id);
            return std::nullopt;
        }
        return blob_path;
    }

  private:
    // For `Tree` the underlying storage type is non-executable file.
    static constexpr ObjectType kStorageType =
        IsTreeObject(kType) ? ObjectType::File : kType;

    Logger logger_{std::string{"ObjectCAS"} + ToChar(kType)};

    FileStorage<kStorageType, StoreMode::FirstWins, /*kSetEpochTime=*/true>
        file_store_;
    gsl::not_null<ExistsFunc> exists_;
    HashFunction const hash_function_;

    /// Default callback for checking blob existence.
    static inline ExistsFunc const kDefaultExists = [](auto const& /*digest*/,
                                                       auto const& path) {
        return FileSystemManager::IsFile(path);
    };

    [[nodiscard]] auto CreateDigest(std::string const& bytes) const noexcept
        -> std::optional<ArtifactDigest> {
        return ArtifactDigest::Create<kType>(hash_function_, bytes);
    }

    [[nodiscard]] auto CreateDigest(std::filesystem::path const& file_path)
        const noexcept -> std::optional<ArtifactDigest> {
        return ArtifactDigest::CreateFromFile<kType>(hash_function_, file_path);
    }

    [[nodiscard]] auto IsAvailable(
        ArtifactDigest const& digest,
        std::filesystem::path const& path) const noexcept -> bool {
        try {
            return std::invoke(exists_.get(), digest, path);
        } catch (...) {
            return false;
        }
    }

    /// \brief Store blob from bytes to storage.
    [[nodiscard]] auto StoreBlobData(std::string const& blob_id,
                                     std::string const& bytes,
                                     bool /*unused*/) const noexcept -> bool {
        return file_store_.AddFromBytes(blob_id, bytes);
    }

    /// \brief Store blob from file path to storage.
    [[nodiscard]] auto StoreBlobData(std::string const& blob_id,
                                     std::filesystem::path const& file_path,
                                     bool is_owner) const noexcept -> bool {
        return file_store_.AddFromFile(blob_id, file_path, is_owner);
    }

    /// \brief Store blob from unspecified data to storage.
    template <class T>
    [[nodiscard]] auto StoreBlob(T const& data, bool is_owner) const noexcept
        -> std::optional<ArtifactDigest> {
        if (auto digest = CreateDigest(data)) {
            auto const& id = digest->hash();
            if (IsAvailable(*digest, file_store_.GetPath(id))) {
                return digest;
            }
            if (StoreBlobData(id, data, is_owner)) {
                return digest;
            }
            logger_.Emit(LogLevel::Debug, "Failed to store blob {}.", id);
        }
        logger_.Emit(LogLevel::Debug, "Failed to create digest.");
        return std::nullopt;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_CAS_HPP
