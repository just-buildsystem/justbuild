#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/local_action.hpp"
#include "src/buildtool/execution_api/local/local_storage.hpp"

/// \brief API for local execution.
class LocalApi final : public IExecutionApi {
  public:
    auto CreateAction(
        ArtifactDigest const& root_digest,
        std::vector<std::string> const& command,
        std::vector<std::string> const& output_files,
        std::vector<std::string> const& output_dirs,
        std::map<std::string, std::string> const& env_vars,
        std::map<std::string, std::string> const& properties) noexcept
        -> IExecutionAction::Ptr final {
        return IExecutionAction::Ptr{new LocalAction{storage_,
                                                     root_digest,
                                                     command,
                                                     output_files,
                                                     output_dirs,
                                                     env_vars,
                                                     properties}};
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths) noexcept
        -> bool final {
        if (artifacts_info.size() != output_paths.size()) {
            Logger::Log(LogLevel::Error,
                        "different number of digests and output paths.");
            return false;
        }

        for (std::size_t i{}; i < artifacts_info.size(); ++i) {
            auto const& info = artifacts_info[i];
            if (IsTreeObject(info.type)) {
                // read object infos from sub tree and call retrieve recursively
                auto const infos =
                    storage_->ReadTreeInfos(info.digest, output_paths[i]);
                if (not infos or
                    not RetrieveToPaths(infos->second, infos->first)) {
                    return false;
                }
            }
            else {
                auto const blob_path = storage_->BlobPath(
                    info.digest, IsExecutableObject(info.type));
                if (not blob_path or
                    not FileSystemManager::CreateDirectory(
                        output_paths[i].parent_path()) or
                    not FileSystemManager::CopyFileAs</*kSetEpochTime=*/true,
                                                      /*kSetWritable=*/true>(
                        *blob_path, output_paths[i], info.type)) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds,
        bool raw_tree) noexcept -> bool final {
        if (artifacts_info.size() != fds.size()) {
            Logger::Log(LogLevel::Error,
                        "different number of digests and file descriptors.");
            return false;
        }

        for (std::size_t i{}; i < artifacts_info.size(); ++i) {
            auto fd = fds[i];
            auto const& info = artifacts_info[i];

            if (gsl::owner<FILE*> out = fdopen(fd, "wb")) {  // NOLINT
                auto const success =
                    storage_->DumpToStream(info, out, raw_tree);
                std::fclose(out);
                if (not success) {
                    Logger::Log(LogLevel::Error,
                                "dumping {} {} to file descriptor {} failed.",
                                IsTreeObject(info.type) ? "tree" : "blob",
                                info.ToString(),
                                fd);
                    return false;
                }
            }
            else {
                Logger::Log(LogLevel::Error,
                            "dumping to file descriptor {} failed.",
                            fd);
                return false;
            }
        }
        return true;
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        gsl::not_null<IExecutionApi*> const& api) noexcept -> bool final {

        // Determine missing artifacts in other CAS.
        std::vector<ArtifactDigest> digests;
        digests.reserve(artifacts_info.size());
        std::unordered_map<ArtifactDigest, Artifact::ObjectInfo> info_map;
        for (auto const& info : artifacts_info) {
            digests.push_back(info.digest);
            info_map[info.digest] = info;
        }
        auto const& missing_digests = api->IsAvailable(digests);
        std::vector<Artifact::ObjectInfo> missing_artifacts_info;
        missing_artifacts_info.reserve(missing_digests.size());
        for (auto const& digest : missing_digests) {
            missing_artifacts_info.push_back(info_map[digest]);
        }

        // Collect blobs of missing artifacts from local CAS. Trees are
        // processed recursively before any blob is uploaded.
        BlobContainer container{};
        for (auto const& info : missing_artifacts_info) {
            // Recursively process trees.
            if (IsTreeObject(info.type)) {
                auto const& infos = storage_->ReadTreeInfos(
                    info.digest, std::filesystem::path{});
                if (not infos or not RetrieveToCas(infos->second, api)) {
                    return false;
                }
            }

            // Determine artifact path.
            auto const& path =
                IsTreeObject(info.type)
                    ? storage_->TreePath(info.digest)
                    : storage_->BlobPath(info.digest,
                                         IsExecutableObject(info.type));
            if (not path) {
                return false;
            }

            // Read artifact content.
            auto const& content = FileSystemManager::ReadFile(*path);
            if (not content) {
                return false;
            }

            // Regenerate digest since object infos read by
            // storage_->ReadTreeInfos() will contain 0 as size.
            ArtifactDigest digest;
            if (IsTreeObject(info.type)) {
                digest = ArtifactDigest::Create<ObjectType::Tree>(*content);
            }
            else {
                digest = ArtifactDigest::Create<ObjectType::File>(*content);
            }

            // Collect blob.
            try {
                container.Emplace(BazelBlob{digest, *content});
            } catch (std::exception const& ex) {
                Logger::Log(
                    LogLevel::Error, "failed to emplace blob: ", ex.what());
                return false;
            }
        }

        // Upload blobs to remote CAS.
        return api->Upload(container, /*skip_find_missing=*/true);
    }

    [[nodiscard]] auto Upload(BlobContainer const& blobs,
                              bool /*skip_find_missing*/) noexcept
        -> bool final {
        for (auto const& blob : blobs) {
            auto const is_tree = NativeSupport::IsTree(blob.digest.hash());
            auto cas_digest = is_tree ? storage_->StoreTree(blob.data)
                                      : storage_->StoreBlob(blob.data);
            if (not cas_digest or not std::equal_to<bazel_re::Digest>{}(
                                      *cas_digest, blob.digest)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const&
            artifacts) noexcept -> std::optional<ArtifactDigest> final {
        BlobContainer blobs{};
        auto digest = BazelMsgFactory::CreateDirectoryDigestFromTree(
            artifacts,
            [&blobs](BazelBlob&& blob) { blobs.Emplace(std::move(blob)); });
        if (not digest) {
            Logger::Log(LogLevel::Debug, "failed to create digest for tree.");
            return std::nullopt;
        }

        if (not Upload(blobs, /*skip_find_missing=*/false)) {
            Logger::Log(LogLevel::Debug, "failed to upload blobs for tree.");
            return std::nullopt;
        }

        return ArtifactDigest{*digest};
    }

    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool final {
        return static_cast<bool>(
            NativeSupport::IsTree(static_cast<bazel_re::Digest>(digest).hash())
                ? storage_->TreePath(digest)
                : storage_->BlobPath(digest, false));
    }

    [[nodiscard]] auto IsAvailable(std::vector<ArtifactDigest> const& digests)
        const noexcept -> std::vector<ArtifactDigest> final {
        std::vector<ArtifactDigest> result;
        for (auto const& digest : digests) {
            auto const& path = NativeSupport::IsTree(
                                   static_cast<bazel_re::Digest>(digest).hash())
                                   ? storage_->TreePath(digest)
                                   : storage_->BlobPath(digest, false);
            if (not path) {
                result.push_back(digest);
            }
        }
        return result;
    }

  private:
    std::shared_ptr<LocalStorage> storage_{std::make_shared<LocalStorage>()};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP
