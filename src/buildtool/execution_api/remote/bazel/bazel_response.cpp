#include "src/buildtool/execution_api/remote/bazel/bazel_response.hpp"

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/logging/logger.hpp"

auto BazelResponse::ReadStringBlob(bazel_re::Digest const& id) noexcept
    -> std::string {
    auto blobs = network_->ReadBlobs({id}).Next();
    if (blobs.empty()) {
        // TODO(oreiche): logging
        return std::string{};
    }
    return blobs[0].data;
}

auto BazelResponse::Artifacts() const noexcept -> ArtifactInfos {
    ArtifactInfos artifacts{};
    auto const& action_result = output_.action_result;
    artifacts.reserve(
        static_cast<std::size_t>(action_result.output_files().size()) +
        static_cast<std::size_t>(action_result.output_directories().size()));

    // collect files and store them
    for (auto const& file : action_result.output_files()) {
        try {
            artifacts.emplace(file.path(),
                              Artifact::ObjectInfo{
                                  ArtifactDigest{file.digest()},
                                  file.is_executable() ? ObjectType::Executable
                                                       : ObjectType::File});
        } catch (...) {
            return {};
        }
    }

    // obtain tree digests for output directories
    std::vector<bazel_re::Digest> tree_digests{};
    tree_digests.reserve(
        gsl::narrow<std::size_t>(action_result.output_directories_size()));
    std::transform(action_result.output_directories().begin(),
                   action_result.output_directories().end(),
                   std::back_inserter(tree_digests),
                   [](auto dir) { return dir.tree_digest(); });

    // collect root digests from trees and store them
    auto blob_reader = network_->ReadBlobs(tree_digests);
    auto tree_blobs = blob_reader.Next();
    std::size_t pos{};
    while (not tree_blobs.empty()) {
        for (auto const& tree_blob : tree_blobs) {
            try {
                auto tree = BazelMsgFactory::MessageFromString<bazel_re::Tree>(
                    tree_blob.data);
                if (not tree) {
                    return {};
                }

                // The server does not store the Directory messages it just has
                // sent us as part of the Tree message. If we want to be able to
                // use the Directories as inputs for actions, we have to upload
                // them manually.
                auto root_digest = UploadTreeMessageDirectories(*tree);
                if (not root_digest) {
                    return {};
                }
                artifacts.emplace(
                    action_result.output_directories(pos).path(),
                    Artifact::ObjectInfo{*root_digest, ObjectType::Tree});
            } catch (...) {
                return {};
            }
            ++pos;
        }
        tree_blobs = blob_reader.Next();
    }
    return artifacts;
}

auto BazelResponse::UploadTreeMessageDirectories(
    bazel_re::Tree const& tree) const -> std::optional<ArtifactDigest> {
    BlobContainer dir_blobs{};

    auto rootdir_blob = ProcessDirectoryMessage(tree.root());
    if (not rootdir_blob) {
        return std::nullopt;
    }
    auto root_digest = rootdir_blob->digest;
    dir_blobs.Emplace(std::move(*rootdir_blob));

    for (auto const& subdir : tree.children()) {
        auto subdir_blob = ProcessDirectoryMessage(subdir);
        if (not subdir_blob) {
            return std::nullopt;
        }
        dir_blobs.Emplace(std::move(*subdir_blob));
    }

    if (not network_->UploadBlobs(dir_blobs)) {
        Logger::Log(LogLevel::Error,
                    "uploading Tree's Directory messages failed");
        return std::nullopt;
    }
    return ArtifactDigest{root_digest};
}

auto BazelResponse::ProcessDirectoryMessage(
    bazel_re::Directory const& dir) const noexcept -> std::optional<BazelBlob> {
    auto data = dir.SerializeAsString();
    auto digest = ArtifactDigest::Create(data);

    if (tree_map_ and not tree_map_->HasTree(digest)) {
        // cache in local tree map
        auto tree = tree_map_->CreateTree();
        if (not BazelMsgFactory::ReadObjectInfosFromDirectory(
                dir,
                [&tree](auto path, auto info) {
                    return tree.AddInfo(path, info);
                }) or
            not tree_map_->AddTree(digest, std::move(tree))) {
            return std::nullopt;
        }
    }

    return BazelBlob{std::move(digest), std::move(data)};
}
