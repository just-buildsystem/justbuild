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
#include <functional>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"

namespace {

auto ProcessDirectoryMessage(HashFunction hash_function,
                             bazel_re::Directory const& dir) noexcept
    -> BazelBlob {
    auto data = dir.SerializeAsString();
    auto digest =
        BazelDigestFactory::HashDataAs<ObjectType::File>(hash_function, data);
    return BazelBlob{std::move(digest),
                     std::move(data),
                     /*is_exec=*/false};
}

}  // namespace

auto BazelResponse::ReadStringBlob(bazel_re::Digest const& id) noexcept
    -> std::string {
    auto reader = network_->CreateReader();
    if (auto blob = reader.ReadSingleBlob(id)) {
        return *blob->data;
    }
    Logger::Log(LogLevel::Warning,
                "reading digest {} from action response failed",
                id.hash());
    return std::string{};
}

auto BazelResponse::Artifacts() noexcept
    -> expected<gsl::not_null<ArtifactInfos const*>, std::string> {
    if (auto error_msg = Populate()) {
        return unexpected{*std::move(error_msg)};
    }
    return gsl::not_null<ArtifactInfos const*>(
        &artifacts_);  // explicit type needed for expected
}

auto BazelResponse::DirectorySymlinks() noexcept
    -> expected<gsl::not_null<DirSymlinks const*>, std::string> {
    if (auto error_msg = Populate()) {
        return unexpected{*std::move(error_msg)};
    }
    return gsl::not_null<DirSymlinks const*>(
        &dir_symlinks_);  // explicit type needed for expected
}

auto BazelResponse::Populate() noexcept -> std::optional<std::string> {
    // Initialized only once lazily
    if (populated_) {
        return std::nullopt;
    }
    populated_ = true;

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

    auto const hash_type = network_->GetHashFunction().GetType();
    // collect files and store them
    for (auto const& file : action_result.output_files()) {
        auto digest =
            ArtifactDigestFactory::FromBazel(hash_type, file.digest());
        if (not digest) {
            return fmt::format(
                "BazelResponse: failed to create artifact digest for {}",
                file.path());
        }
        try {
            artifacts.emplace(
                file.path(),
                Artifact::ObjectInfo{.digest = *std::move(digest),
                                     .type = file.is_executable()
                                                 ? ObjectType::Executable
                                                 : ObjectType::File});
        } catch (std::exception const& ex) {
            return fmt::format(
                "BazelResponse: unexpected failure gathering digest for "
                "{}:\n{}",
                file.path(),
                ex.what());
        }
    }

    // collect all symlinks and store them
    for (auto const& link : action_result.output_file_symlinks()) {
        try {
            artifacts.emplace(
                link.path(),
                Artifact::ObjectInfo{
                    .digest =
                        ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                            network_->GetHashFunction(), link.target()),
                    .type = ObjectType::Symlink});
        } catch (std::exception const& ex) {
            return fmt::format(
                "BazelResponse: unexpected failure gathering digest for "
                "{}:\n{}",
                link.path(),
                ex.what());
        }
    }
    for (auto const& link : action_result.output_directory_symlinks()) {
        try {
            artifacts.emplace(
                link.path(),
                Artifact::ObjectInfo{
                    .digest =
                        ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                            network_->GetHashFunction(), link.target()),
                    .type = ObjectType::Symlink});
            dir_symlinks.emplace(link.path());  // add it to set
        } catch (std::exception const& ex) {
            return fmt::format(
                "BazelResponse: unexpected failure gathering digest for "
                "{}:\n{}",
                link.path(),
                ex.what());
        }
    }

    if (ProtocolTraits::IsNative(hash_type)) {
        // in native mode: just collect and store tree digests
        for (auto const& tree : action_result.output_directories()) {
            auto digest =
                ArtifactDigestFactory::FromBazel(hash_type, tree.tree_digest());
            if (not digest) {
                return fmt::format(
                    "BazelResponse: failed to create artifact digest for {}",
                    tree.path());
            }
            ExpectsAudit(digest->IsTree());
            try {
                artifacts.emplace(
                    tree.path(),
                    Artifact::ObjectInfo{.digest = *std::move(digest),
                                         .type = ObjectType::Tree});
            } catch (std::exception const& ex) {
                return fmt::format(
                    "BazelResponse: unexpected failure gathering digest for "
                    "{}:\n{}",
                    tree.path(),
                    ex.what());
            }
        }
        artifacts_ = std::move(artifacts);
        dir_symlinks_ = std::move(dir_symlinks);
        return std::nullopt;
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
                    return fmt::format(
                        "BazelResponse: failed to create Tree for {}",
                        tree_blob.digest.hash());
                }

                // The server does not store the Directory messages it just
                // has sent us as part of the Tree message. If we want to be
                // able to use the Directories as inputs for actions, we
                // have to upload them manually.
                auto root_digest = UploadTreeMessageDirectories(*tree);
                if (not root_digest) {
                    auto const error = fmt::format(
                        "BazelResponse: failure in uploading Tree Directory "
                        "message for {}",
                        tree_blob.digest.hash());
                    Logger::Log(LogLevel::Trace, error);
                    return error;
                }
                artifacts.emplace(
                    action_result.output_directories(pos).path(),
                    Artifact::ObjectInfo{.digest = *root_digest,
                                         .type = ObjectType::Tree});
            } catch (std::exception const& ex) {
                return fmt::format(
                    "BazelResponse: unexpected failure gathering digest for "
                    "{}:\n{}",
                    tree_blob.digest.hash(),
                    ex.what());
            }
            ++pos;
        }
    }
    artifacts_ = std::move(artifacts);
    dir_symlinks_ = std::move(dir_symlinks);
    return std::nullopt;
}

auto BazelResponse::UploadTreeMessageDirectories(
    bazel_re::Tree const& tree) const -> std::optional<ArtifactDigest> {
    auto const upload_callback =
        [&network = *network_](BazelBlobContainer&& blobs) -> bool {
        return network.UploadBlobs(std::move(blobs));
    };
    auto const hash_function = network_->GetHashFunction();
    BazelBlobContainer dir_blobs{};

    auto rootdir_blob = ProcessDirectoryMessage(hash_function, tree.root());
    auto const root_digest = rootdir_blob.digest;
    // store or upload rootdir blob, taking maximum transfer size into account
    if (not UpdateContainerAndUpload<bazel_re::Digest>(
            &dir_blobs,
            std::move(rootdir_blob),
            /*exception_is_fatal=*/false,
            upload_callback)) {
        return std::nullopt;
    }

    for (auto const& subdir : tree.children()) {
        // store or upload blob, taking maximum transfer size into account
        if (not UpdateContainerAndUpload<bazel_re::Digest>(
                &dir_blobs,
                ProcessDirectoryMessage(hash_function, subdir),
                /*exception_is_fatal=*/false,
                upload_callback)) {
            return std::nullopt;
        }
    }

    // upload any remaining blob
    if (not std::invoke(upload_callback, std::move(dir_blobs))) {
        return std::nullopt;
    }
    return ArtifactDigestFactory::FromBazel(hash_function.GetType(),
                                            root_digest)
        .value();  // must succeed all the time
}
