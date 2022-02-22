#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_STORAGE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_STORAGE_HPP

#include <optional>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/common/local_tree_map.hpp"
#include "src/buildtool/execution_api/local/local_ac.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"

class LocalStorage {
  public:
    explicit LocalStorage(
        std::shared_ptr<LocalTreeMap> tree_map = nullptr) noexcept
        : tree_map_{std::move(tree_map)} {}

    explicit LocalStorage(
        std::filesystem::path const& cache_root,
        std::shared_ptr<LocalTreeMap> tree_map = nullptr) noexcept
        : cas_file_{cache_root},
          cas_exec_{cache_root},
          ac_{&cas_file_, cache_root},
          tree_map_{std::move(tree_map)} {}

    /// \brief Store blob from file path with x-bit determined from file system.
    [[nodiscard]] auto StoreBlob(std::filesystem::path const& file_path)
        const noexcept -> std::optional<bazel_re::Digest> {
        return StoreBlob(file_path, FileSystemManager::IsExecutable(file_path));
    }

    /// \brief Store blob from file path with x-bit.
    [[nodiscard]] auto StoreBlob(std::filesystem::path const& file_path,
                                 bool is_executable) const noexcept
        -> std::optional<bazel_re::Digest> {
        if (is_executable) {
            return cas_exec_.StoreBlobFromFile(file_path);
        }
        return cas_file_.StoreBlobFromFile(file_path);
    }

    /// \brief Store blob from bytes with x-bit (default: non-executable).
    [[nodiscard]] auto StoreBlob(std::string const& bytes,
                                 bool is_executable = false) const noexcept
        -> std::optional<bazel_re::Digest> {
        return is_executable ? cas_exec_.StoreBlobFromBytes(bytes)
                             : cas_file_.StoreBlobFromBytes(bytes);
    }

    /// \brief Obtain blob path from digest with x-bit.
    [[nodiscard]] auto BlobPath(bazel_re::Digest const& digest,
                                bool is_executable) const noexcept
        -> std::optional<std::filesystem::path> {
        auto const path = is_executable ? cas_exec_.BlobPath(digest)
                                        : cas_file_.BlobPath(digest);
        return path ? path : TrySyncBlob(digest, is_executable);
    }

    [[nodiscard]] auto StoreActionResult(
        bazel_re::Digest const& action_id,
        bazel_re::ActionResult const& result) const noexcept -> bool {
        return ac_.StoreResult(action_id, result);
    }

    [[nodiscard]] auto CachedActionResult(bazel_re::Digest const& action_id)
        const noexcept -> std::optional<bazel_re::ActionResult> {
        return ac_.CachedResult(action_id);
    }

    [[nodiscard]] auto ReadTreeInfos(
        bazel_re::Digest const& tree_digest,
        std::filesystem::path const& parent) const noexcept
        -> std::optional<std::pair<std::vector<std::filesystem::path>,
                                   std::vector<Artifact::ObjectInfo>>>;

    [[nodiscard]] auto DumpToStream(
        Artifact::ObjectInfo const& info,
        gsl::not_null<FILE*> const& stream) const noexcept -> bool;

  private:
    LocalCAS<ObjectType::File> cas_file_{};
    LocalCAS<ObjectType::Executable> cas_exec_{};
    LocalAC ac_{&cas_file_};
    std::shared_ptr<LocalTreeMap> tree_map_;

    /// \brief Try to sync blob between file CAS and executable CAS.
    /// \param digest        Blob digest.
    /// \param to_executable Sync direction.
    /// \returns Path to blob in target CAS.
    [[nodiscard]] auto TrySyncBlob(bazel_re::Digest const& digest,
                                   bool to_executable) const noexcept
        -> std::optional<std::filesystem::path> {
        std::optional<std::filesystem::path> const src_blob{
            to_executable ? cas_file_.BlobPath(digest)
                          : cas_exec_.BlobPath(digest)};
        if (src_blob and StoreBlob(*src_blob, to_executable)) {
            return BlobPath(digest, to_executable);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto ReadObjectInfosRecursively(
        BazelMsgFactory::InfoStoreFunc const& store_info,
        std::filesystem::path const& parent,
        bazel_re::Digest const& digest) const noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_STORAGE_HPP
