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
#include <exception>
#include <filesystem>
#include <functional>
#include <unordered_set>
#include <vector>

#include "fmt/core.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/content_blob_container.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network_reader.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/path.hpp"

namespace {

auto ProcessDirectoryMessage(HashFunction hash_function,
                             bazel_re::Directory const& dir) noexcept
    -> expected<ArtifactBlob, std::string> {
    // in compatible mode: check validity of all symlinks
    for (auto const& link : dir.symlinks()) {
        if (not PathIsNonUpwards(link.target())) {
            return unexpected{
                fmt::format("found invalid symlink at {}", link.name())};
        }
    }
    auto data = dir.SerializeAsString();
    auto digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, data);
    return ArtifactBlob{std::move(digest),
                        std::move(data),
                        /*is_exec=*/false};
}

}  // namespace

auto BazelResponse::ReadStringBlob(bazel_re::Digest const& id) noexcept
    -> std::string {
    auto digest = ArtifactDigestFactory::FromBazel(
        network_->GetHashFunction().GetType(), id);
    if (digest.has_value()) {
        auto reader = network_->CreateReader();
        if (auto blob = reader.ReadSingleBlob(*digest)) {
            return *blob->data;
        }
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
            // in compatible mode: check symlink validity
            if (not ProtocolTraits::IsNative(
                    network_->GetHashFunction().GetType()) and
                not PathIsNonUpwards(link.target())) {
                return fmt::format("BazelResponse: found invalid symlink at {}",
                                   link.path());
            }
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
            // in compatible mode: check symlink validity
            if (not ProtocolTraits::IsNative(
                    network_->GetHashFunction().GetType()) and
                not PathIsNonUpwards(link.target())) {
                return fmt::format("BazelResponse: found invalid symlink at {}",
                                   link.path());
            }
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
    std::vector<ArtifactDigest> tree_digests{};
    tree_digests.reserve(
        gsl::narrow<std::size_t>(action_result.output_directories_size()));
    for (auto const& directory : action_result.output_directories()) {
        auto digest = ArtifactDigestFactory::FromBazel(
            network_->GetHashFunction().GetType(), directory.tree_digest());
        if (not digest) {
            return std::move(digest).error();
        }
        tree_digests.push_back(*std::move(digest));
    }

    // collect root digests from trees and store them
    auto reader = network_->CreateReader();
    int pos = 0;
    for (auto tree_blobs : reader.ReadIncrementally(&tree_digests)) {
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
                    auto error =
                        fmt::format("BazelResponse: {}", root_digest.error());
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
    bazel_re::Tree const& tree) const -> expected<ArtifactDigest, std::string> {
    auto const upload_callback =
        [&network =
             *network_](std::unordered_set<ArtifactBlob>&& blobs) -> bool {
        return network.UploadBlobs(std::move(blobs));
    };
    auto const hash_function = network_->GetHashFunction();
    std::unordered_set<ArtifactBlob> dir_blobs{};

    auto rootdir_blob = ProcessDirectoryMessage(hash_function, tree.root());
    if (not rootdir_blob) {
        return unexpected{std::move(rootdir_blob).error()};
    }
    auto const root_digest = rootdir_blob->digest;
    // store or upload rootdir blob, taking maximum transfer size into account
    if (not UpdateContainerAndUpload<ArtifactDigest>(
            &dir_blobs,
            *std::move(rootdir_blob),
            /*exception_is_fatal=*/false,
            upload_callback)) {
        return unexpected{fmt::format(
            "failed to upload Tree with root digest {}", root_digest.hash())};
    }

    for (auto const& subdir : tree.children()) {
        // store or upload blob, taking maximum transfer size into account
        auto blob = ProcessDirectoryMessage(hash_function, subdir);
        if (not blob) {
            return unexpected{std::move(blob).error()};
        }
        auto const blob_digest = blob->digest;
        if (not UpdateContainerAndUpload<ArtifactDigest>(
                &dir_blobs,
                *std::move(blob),
                /*exception_is_fatal=*/false,
                upload_callback)) {
            return unexpected{
                fmt::format("failed to upload Tree subdir with digest {}",
                            blob_digest.hash())};
        }
    }

    // upload any remaining blob
    if (not std::invoke(upload_callback, std::move(dir_blobs))) {
        return unexpected{
            fmt::format("failed to upload blobs for Tree with root digest {}",
                        root_digest.hash())};
    }
    return root_digest;
}
