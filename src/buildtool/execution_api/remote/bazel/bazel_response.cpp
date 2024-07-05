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

#include "src/buildtool/execution_api/remote/bazel/bazel_response.hpp"

#include <cstddef>

#include "gsl/gsl"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"

namespace {

auto ProcessDirectoryMessage(bazel_re::Directory const& dir) noexcept
    -> std::optional<BazelBlob> {
    auto data = dir.SerializeAsString();
    auto digest = ArtifactDigest::Create<ObjectType::File>(
        HashFunction::Instance(), data);
    return BazelBlob{std::move(digest), std::move(data), /*is_exec=*/false};
}

}  // namespace

auto BazelResponse::ReadStringBlob(bazel_re::Digest const& id) noexcept
    -> std::string {
    auto reader = network_->CreateReader();
    if (auto blob = reader.ReadSingleBlob(ArtifactDigest{id})) {
        return *blob->data;
    }
    Logger::Log(LogLevel::Warning,
                "reading digest {} from action response failed",
                id.hash());
    return std::string{};
}

auto BazelResponse::Artifacts() noexcept -> ArtifactInfos {
    return ArtifactsWithDirSymlinks().first;
}

auto BazelResponse::ArtifactsWithDirSymlinks() noexcept
    -> std::pair<ArtifactInfos, DirSymlinks> {
    // make sure to populate only once
    populated_ = Populate();
    if (not populated_) {
        return {};
    }
    return std::pair(artifacts_, dir_symlinks_);
}

auto BazelResponse::Populate() noexcept -> bool {
    if (populated_) {
        return true;
    }
    ArtifactInfos artifacts{};
    auto const& action_result = output_.action_result;
    artifacts.reserve(
        static_cast<std::size_t>(action_result.output_files_size()) +
        static_cast<std::size_t>(action_result.output_file_symlinks_size()) +
        static_cast<std::size_t>(
            action_result.output_directory_symlinks_size()) +
        static_cast<std::size_t>(action_result.output_directories_size()));

    DirSymlinks dir_symlinks{};
    dir_symlinks.reserve(static_cast<std::size_t>(
        action_result.output_directory_symlinks_size()));

    // collect files and store them
    for (auto const& file : action_result.output_files()) {
        try {
            artifacts.emplace(
                file.path(),
                Artifact::ObjectInfo{.digest = ArtifactDigest{file.digest()},
                                     .type = file.is_executable()
                                                 ? ObjectType::Executable
                                                 : ObjectType::File});
        } catch (...) {
            return false;
        }
    }

    // collect all symlinks and store them
    for (auto const& link : action_result.output_file_symlinks()) {
        try {
            artifacts.emplace(
                link.path(),
                Artifact::ObjectInfo{
                    .digest = ArtifactDigest::Create<ObjectType::File>(
                        HashFunction::Instance(), link.target()),
                    .type = ObjectType::Symlink});
        } catch (...) {
            return false;
        }
    }
    for (auto const& link : action_result.output_directory_symlinks()) {
        try {
            artifacts.emplace(
                link.path(),
                Artifact::ObjectInfo{
                    .digest = ArtifactDigest::Create<ObjectType::File>(
                        HashFunction::Instance(), link.target()),
                    .type = ObjectType::Symlink});
            dir_symlinks.emplace(link.path());  // add it to set
        } catch (...) {
            return false;
        }
    }

    // collect all symlinks and store them
    for (auto const& link : action_result.output_file_symlinks()) {
        try {
            artifacts.emplace(
                link.path(),
                Artifact::ObjectInfo{
                    .digest = ArtifactDigest::Create<ObjectType::File>(
                        HashFunction::Instance(), link.target()),
                    .type = ObjectType::Symlink});
        } catch (...) {
            return false;
        }
    }
    for (auto const& link : action_result.output_directory_symlinks()) {
        try {
            artifacts.emplace(
                link.path(),
                Artifact::ObjectInfo{
                    .digest = ArtifactDigest::Create<ObjectType::File>(
                        HashFunction::Instance(), link.target()),
                    .type = ObjectType::Symlink});
        } catch (...) {
            return false;
        }
    }

    if (not Compatibility::IsCompatible()) {
        // in native mode: just collect and store tree digests
        for (auto const& tree : action_result.output_directories()) {
            ExpectsAudit(NativeSupport::IsTree(tree.tree_digest().hash()));
            try {
                artifacts.emplace(
                    tree.path(),
                    Artifact::ObjectInfo{
                        .digest = ArtifactDigest{tree.tree_digest()},
                        .type = ObjectType::Tree});
            } catch (...) {
                return false;
            }
        }
        artifacts_ = std::move(artifacts);
        dir_symlinks_ = std::move(dir_symlinks);
        return true;
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
    auto reader = network_->CreateReader();
    int pos = 0;
    for (auto tree_blobs : reader.ReadIncrementally(tree_digests)) {
        for (auto const& tree_blob : tree_blobs) {
            try {
                auto tree = BazelMsgFactory::MessageFromString<bazel_re::Tree>(
                    *tree_blob.data);
                if (not tree) {
                    return false;
                }

                // The server does not store the Directory messages it just
                // has sent us as part of the Tree message. If we want to be
                // able to use the Directories as inputs for actions, we
                // have to upload them manually.
                auto root_digest = UploadTreeMessageDirectories(*tree);
                if (not root_digest) {
                    return false;
                }
                artifacts.emplace(
                    action_result.output_directories(pos).path(),
                    Artifact::ObjectInfo{.digest = *root_digest,
                                         .type = ObjectType::Tree});
            } catch (...) {
                return false;
            }
            ++pos;
        }
    }
    artifacts_ = std::move(artifacts);
    dir_symlinks_ = std::move(dir_symlinks);
    return true;
}

auto BazelResponse::UploadTreeMessageDirectories(
    bazel_re::Tree const& tree) const -> std::optional<ArtifactDigest> {
    BazelBlobContainer dir_blobs{};

    auto rootdir_blob = ProcessDirectoryMessage(tree.root());
    if (not rootdir_blob) {
        return std::nullopt;
    }
    auto root_digest = rootdir_blob->digest;
    // store or upload rootdir blob, taking maximum transfer size into account
    if (not UpdateContainerAndUpload<bazel_re::Digest>(
            &dir_blobs,
            std::move(*rootdir_blob),
            /*exception_is_fatal=*/false,
            [&network = network_](BazelBlobContainer&& blobs) {
                return network->UploadBlobs(std::move(blobs));
            })) {
        Logger::Log(LogLevel::Error,
                    "uploading Tree's Directory messages failed");
        return std::nullopt;
    }

    for (auto const& subdir : tree.children()) {
        auto subdir_blob = ProcessDirectoryMessage(subdir);
        if (not subdir_blob) {
            return std::nullopt;
        }
        // store or upload blob, taking maximum transfer size into account
        if (not UpdateContainerAndUpload<bazel_re::Digest>(
                &dir_blobs,
                std::move(*subdir_blob),
                /*exception_is_fatal=*/false,
                [&network = network_](BazelBlobContainer&& blobs) {
                    return network->UploadBlobs(std::move(blobs));
                })) {
            Logger::Log(LogLevel::Error,
                        "uploading Tree's Directory messages failed");
            return std::nullopt;
        }
    }

    // upload any remaining blob
    if (not network_->UploadBlobs(std::move(dir_blobs))) {
        Logger::Log(LogLevel::Error,
                    "uploading Tree's Directory messages failed");
        return std::nullopt;
    }
    return ArtifactDigest{root_digest};
}
