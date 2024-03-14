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

#include <memory>
#include <sstream>
#include <thread>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/bazel_types.hpp"
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
    using ExistsFunc = std::function<bool(bazel_re::Digest const&,
                                          std::filesystem::path const&)>;

    /// Default callback for checking blob existence.
    static inline ExistsFunc const kDefaultExists = [](auto /*digest*/,
                                                       auto path) {
        return FileSystemManager::IsFile(path);
    };

    /// \brief Create new object CAS in store_path directory.
    /// The optional "exists callback" is used to check blob existence before
    /// every read and write operation. It promises that a blob for the given
    /// digest exists at the given path if true was returned.
    /// \param store_path   The path to use for storing blobs.
    /// \param exists       (optional) Function for checking blob existence.
    explicit ObjectCAS(std::filesystem::path const& store_path,
                       ExistsFunc exists = kDefaultExists)
        : file_store_{store_path}, exists_{std::move(exists)} {}

    ObjectCAS(ObjectCAS const&) = delete;
    ObjectCAS(ObjectCAS&&) = delete;
    auto operator=(ObjectCAS const&) -> ObjectCAS& = delete;
    auto operator=(ObjectCAS&&) -> ObjectCAS& = delete;
    ~ObjectCAS() noexcept = default;

    /// \brief Store blob from bytes.
    /// \param bytes    The bytes do create the blob from.
    /// \returns Digest of the stored blob or nullopt in case of error.
    [[nodiscard]] auto StoreBlobFromBytes(std::string const& bytes)
        const noexcept -> std::optional<bazel_re::Digest> {
        return StoreBlob(bytes, /*is_owner=*/true);
    }

    /// \brief Store blob from file path.
    /// \param file_path    The path of the file to store as blob.
    /// \param is_owner     Indicates ownership for optimization (hardlink).
    /// \returns Digest of the stored blob or nullopt in case of error.
    [[nodiscard]] auto StoreBlobFromFile(std::filesystem::path const& file_path,
                                         bool is_owner = false) const noexcept
        -> std::optional<bazel_re::Digest> {
        return StoreBlob(file_path, is_owner);
    }

    /// \brief Get path to blob.
    /// \param digest   Digest of the blob to lookup.
    /// \returns Path to blob if found or nullopt otherwise.
    [[nodiscard]] auto BlobPath(bazel_re::Digest const& digest) const noexcept
        -> std::optional<std::filesystem::path> {
        auto id = NativeSupport::Unprefix(digest.hash());
        auto blob_path = file_store_.GetPath(id);
        if (not IsAvailable(digest, blob_path)) {
            logger_.Emit(LogLevel::Debug, "Blob not found {}", id);
            return std::nullopt;
        }
        return blob_path;
    }

  private:
    // For `Tree` the underlying storage type is non-executable file.
    static constexpr auto kStorageType =
        kType == ObjectType::Tree ? ObjectType::File : kType;

    Logger logger_{std::string{"ObjectCAS"} + ToChar(kType)};

    FileStorage<kStorageType, StoreMode::FirstWins, /*kSetEpochTime=*/true>
        file_store_;
    ExistsFunc exists_;

    [[nodiscard]] static auto CreateDigest(std::string const& bytes) noexcept
        -> std::optional<bazel_re::Digest> {
        return ArtifactDigest::Create<kType>(bytes);
    }

    [[nodiscard]] static auto CreateDigest(
        std::filesystem::path const& file_path) noexcept
        -> std::optional<bazel_re::Digest> {
        bool is_tree = kType == ObjectType::Tree;
        auto hash = HashFunction::ComputeHashFile(file_path, is_tree);
        if (hash) {
            return ArtifactDigest(
                hash->first.HexString(), hash->second, is_tree);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto IsAvailable(
        bazel_re::Digest const& digest,
        std::filesystem::path const& path) const noexcept -> bool {
        try {
            return exists_ and exists_(digest, path);
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
        -> std::optional<bazel_re::Digest> {
        if (auto digest = CreateDigest(data)) {
            auto id = NativeSupport::Unprefix(digest->hash());
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
