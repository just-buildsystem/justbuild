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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_CAS_HPP

#include <sstream>
#include <thread>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

template <ObjectType kType = ObjectType::File>
class LocalCAS {
  public:
    LocalCAS() noexcept = default;

    LocalCAS(LocalCAS const&) = delete;
    LocalCAS(LocalCAS&&) = delete;
    auto operator=(LocalCAS const&) -> LocalCAS& = delete;
    auto operator=(LocalCAS&&) -> LocalCAS& = delete;
    ~LocalCAS() noexcept = default;

    [[nodiscard]] static auto Instance() noexcept -> LocalCAS<kType>& {
        static auto instance = LocalCAS<kType>{};
        return instance;
    }

    [[nodiscard]] auto StoreBlobFromBytes(std::string const& bytes)
        const noexcept -> std::optional<bazel_re::Digest> {
        return StoreBlob(bytes, /*is_owner=*/true);
    }

    [[nodiscard]] auto StoreBlobFromFile(std::filesystem::path const& file_path,
                                         bool is_owner = false) const noexcept
        -> std::optional<bazel_re::Digest> {
        return StoreBlob(file_path, is_owner);
    }

    [[nodiscard]] auto BlobPath(bazel_re::Digest const& digest) const noexcept
        -> std::optional<std::filesystem::path> {
        auto id = NativeSupport::Unprefix(digest.hash());
        auto blob_path = file_store_.GetPath(id);
        if (FileSystemManager::IsFile(blob_path)) {
            return blob_path;
        }
        logger_.Emit(LogLevel::Debug, "Blob not found {}", id);
        return std::nullopt;
    }

  private:
    // For `Tree` the underlying storage type is non-executable file.
    static constexpr auto kStorageType =
        kType == ObjectType::Tree ? ObjectType::File : kType;

    Logger logger_{std::string{"LocalCAS"} + ToChar(kType)};

    FileStorage<kStorageType, StoreMode::FirstWins, /*kSetEpochTime=*/true>
        file_store_{LocalExecutionConfig::CASDir<kType>(0)};

    [[nodiscard]] static auto CreateDigest(std::string const& bytes) noexcept
        -> std::optional<bazel_re::Digest> {
        return ArtifactDigest::Create<kType>(bytes);
    }

    [[nodiscard]] static auto CreateDigest(
        std::filesystem::path const& file_path) noexcept
        -> std::optional<bazel_re::Digest> {
        auto const bytes = FileSystemManager::ReadFile(file_path);
        if (bytes.has_value()) {
            return ArtifactDigest::Create<kType>(*bytes);
        }
        return std::nullopt;
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
        auto digest = CreateDigest(data);
        if (digest) {
            if (StoreBlobData(
                    NativeSupport::Unprefix(digest->hash()), data, is_owner)) {
                return digest;
            }
            logger_.Emit(
                LogLevel::Debug, "Failed to store blob {}.", digest->hash());
        }
        logger_.Emit(LogLevel::Debug, "Failed to create digest.");
        return std::nullopt;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_CAS_HPP
