#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_CAS_HPP

#include <sstream>
#include <thread>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

template <ObjectType kType = ObjectType::File>
class LocalCAS {
  public:
    LocalCAS() noexcept = default;

    explicit LocalCAS(std::filesystem::path cache_root) noexcept
        : cache_root_{std::move(cache_root)} {}

    LocalCAS(LocalCAS const&) = delete;
    LocalCAS(LocalCAS&&) = delete;
    auto operator=(LocalCAS const&) -> LocalCAS& = delete;
    auto operator=(LocalCAS &&) -> LocalCAS& = delete;
    ~LocalCAS() noexcept = default;

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
        auto blob_path = file_store_.GetPath(digest.hash());
        if (FileSystemManager::IsFile(blob_path)) {
            return blob_path;
        }
        logger_.Emit(LogLevel::Debug, "Blob not found {}", digest.hash());
        return std::nullopt;
    }

  private:
    static constexpr char kSuffix = ToChar(kType);
    Logger logger_{std::string{"LocalCAS"} + kSuffix};
    std::filesystem::path const cache_root_{
        LocalExecutionConfig::GetCacheDir()};
    FileStorage<kType> file_store_{cache_root_ /
                                   (std::string{"cas"} + kSuffix)};

    [[nodiscard]] static auto CreateDigest(std::string const& bytes) noexcept
        -> std::optional<bazel_re::Digest> {
        return ArtifactDigest::Create(bytes);
    }

    [[nodiscard]] static auto CreateDigest(
        std::filesystem::path const& file_path) noexcept
        -> std::optional<bazel_re::Digest> {
        auto const bytes = FileSystemManager::ReadFile(file_path);
        if (bytes.has_value()) {
            return ArtifactDigest::Create(*bytes);
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
            if (StoreBlobData(digest->hash(), data, is_owner)) {
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
