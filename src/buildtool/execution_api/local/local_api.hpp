#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/local_tree_map.hpp"
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
                                                     tree_map_,
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
        bool /*raw_tree*/) noexcept -> bool final {
        if (artifacts_info.size() != fds.size()) {
            Logger::Log(LogLevel::Error,
                        "different number of digests and file descriptors.");
            return false;
        }

        for (std::size_t i{}; i < artifacts_info.size(); ++i) {
            auto fd = fds[i];
            auto const& info = artifacts_info[i];

            if (gsl::owner<FILE*> out = fdopen(fd, "wb")) {  // NOLINT
                auto const success = storage_->DumpToStream(info, out);
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

    [[nodiscard]] auto Upload(BlobContainer const& blobs,
                              bool /*skip_find_missing*/) noexcept
        -> bool final {
        for (auto const& blob : blobs) {
            auto cas_digest = storage_->StoreBlob(blob.data);
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
        auto tree = tree_map_->CreateTree();
        auto digest = BazelMsgFactory::CreateDirectoryDigestFromTree(
            artifacts,
            [&blobs](BazelBlob&& blob) { blobs.Emplace(std::move(blob)); },
            [&tree](auto path, auto info) { return tree.AddInfo(path, info); });
        if (not digest) {
            Logger::Log(LogLevel::Debug, "failed to create digest for tree.");
            return std::nullopt;
        }

        if (not Upload(blobs, /*skip_find_missing=*/false)) {
            Logger::Log(LogLevel::Debug, "failed to upload blobs for tree.");
            return std::nullopt;
        }

        if (tree_map_->AddTree(*digest, std::move(tree))) {
            return ArtifactDigest{*digest};
        }
        return std::nullopt;
    }

    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool final {
        return storage_->BlobPath(digest, false).has_value();
    }

    [[nodiscard]] auto IsAvailable(std::vector<ArtifactDigest> const& digests)
        const noexcept -> std::vector<ArtifactDigest> final {
        std::vector<ArtifactDigest> result;
        for (auto const& digest : digests) {
            if (not storage_->BlobPath(digest, false).has_value()) {
                result.push_back(digest);
            }
        }
        return result;
    }

  private:
    std::shared_ptr<LocalTreeMap> tree_map_{std::make_shared<LocalTreeMap>()};
    std::shared_ptr<LocalStorage> storage_{
        std::make_shared<LocalStorage>(tree_map_)};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP
