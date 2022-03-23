#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_ac_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_action.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_response.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

BazelApi::BazelApi(std::string const& instance_name,
                   std::string const& host,
                   Port port,
                   ExecutionConfiguration const& exec_config) noexcept {
    tree_map_ = std::make_shared<LocalTreeMap>();
    network_ = std::make_shared<BazelNetwork>(
        instance_name, host, port, exec_config, tree_map_);
}

// implement move constructor in cpp, where all members are complete types
BazelApi::BazelApi(BazelApi&& other) noexcept = default;

// implement destructor in cpp, where all members are complete types
BazelApi::~BazelApi() = default;

auto BazelApi::CreateAction(
    ArtifactDigest const& root_digest,
    std::vector<std::string> const& command,
    std::vector<std::string> const& output_files,
    std::vector<std::string> const& output_dirs,
    std::map<std::string, std::string> const& env_vars,
    std::map<std::string, std::string> const& properties) noexcept
    -> IExecutionAction::Ptr {
    return std::unique_ptr<BazelAction>{new BazelAction{network_,
                                                        tree_map_,
                                                        root_digest,
                                                        command,
                                                        output_files,
                                                        output_dirs,
                                                        env_vars,
                                                        properties}};
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto BazelApi::RetrieveToPaths(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<std::filesystem::path> const& output_paths) noexcept -> bool {
    if (artifacts_info.size() != output_paths.size()) {
        Logger::Log(LogLevel::Error,
                    "different number of digests and output paths.");
        return false;
    }

    // Obtain file digests from artifact infos
    std::vector<bazel_re::Digest> file_digests{};
    std::vector<std::size_t> artifact_pos{};
    for (std::size_t i{}; i < artifacts_info.size(); ++i) {
        auto const& info = artifacts_info[i];
        if (IsTreeObject(info.type)) {
            // read object infos from sub tree and call retrieve recursively
            auto const infos =
                network_->ReadTreeInfos(info.digest, output_paths[i]);
            if (not infos or not RetrieveToPaths(infos->second, infos->first)) {
                return false;
            }
        }
        else {
            file_digests.emplace_back(info.digest);
            artifact_pos.emplace_back(i);
        }
    }

    // Request file blobs
    auto size = file_digests.size();
    auto reader = network_->ReadBlobs(std::move(file_digests));
    auto blobs = reader.Next();
    std::size_t count{};
    while (not blobs.empty()) {
        if (count + blobs.size() > size) {
            Logger::Log(LogLevel::Error, "received more blobs than requested.");
            return false;
        }
        for (std::size_t pos = 0; pos < blobs.size(); ++pos) {
            auto gpos = artifact_pos[count + pos];
            auto const& type = artifacts_info[gpos].type;
            if (not FileSystemManager::WriteFileAs(
                    blobs[pos].data, output_paths[gpos], type)) {
                return false;
            }
        }
        count += blobs.size();
        blobs = reader.Next();
    }

    if (count != size) {
        Logger::Log(LogLevel::Error, "could not retrieve all requested blobs.");
        return false;
    }

    return true;
}

[[nodiscard]] auto BazelApi::RetrieveToFds(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<int> const& fds) noexcept -> bool {
    if (artifacts_info.size() != fds.size()) {
        Logger::Log(LogLevel::Error,
                    "different number of digests and file descriptors.");
        return false;
    }

    for (std::size_t i{}; i < artifacts_info.size(); ++i) {
        auto fd = fds[i];
        auto const& info = artifacts_info[i];

        if (gsl::owner<FILE*> out = fdopen(fd, "wb")) {  // NOLINT
            auto const success = network_->DumpToStream(info, out);
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
            Logger::Log(
                LogLevel::Error, "opening file descriptor {} failed.", fd);
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto BazelApi::Upload(BlobContainer const& blobs,
                                    bool skip_find_missing) noexcept -> bool {
    return network_->UploadBlobs(blobs, skip_find_missing);
}

[[nodiscard]] auto BazelApi::UploadTree(
    std::vector<DependencyGraph::NamedArtifactNodePtr> const&
        artifacts) noexcept -> std::optional<ArtifactDigest> {
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
        return ArtifactDigest{std::move(*digest)};
    }
    return std::nullopt;
}

[[nodiscard]] auto BazelApi::IsAvailable(
    ArtifactDigest const& digest) const noexcept -> bool {
    return network_->IsAvailable(digest);
}
