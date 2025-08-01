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

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "fmt/core.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network_reader.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/path.hpp"

namespace {

/// \brief Generates an ArtifactBlob from a Directory message and while
/// there, checks whether it includes any upward symlinks and collects
/// all symlinks as separate ArtifactBlobs.
/// \param[in] hash_function  The hash function to be used.
/// \param[in] dir            The Bazel Directory message.
/// \returns In case of success: a tuple containing the ArtifactBlob of
/// the Directory message, the collection of ArtifactBlobs of all
/// contained symbolic links, and a flag whether the message contains
/// upward symlinks. In case of failure: an error message.
auto ProcessDirectoryMessage(HashFunction hash_function,
                             bazel_re::Directory const& dir) noexcept
    -> expected<std::tuple<ArtifactBlob,
                           std::unordered_set<ArtifactBlob>,
                           /*has_upwards_symlinks*/ bool>,
                std::string> {
    // in compatible mode: track upwards symlinks
    bool has_upwards_symlinks = std::any_of(
        dir.symlinks().begin(), dir.symlinks().end(), [](auto const& link) {
            return not PathIsNonUpwards(link.target());
        });
    auto blob = ArtifactBlob::FromMemory(
        hash_function, ObjectType::File, dir.SerializeAsString());
    if (not blob) {
        return unexpected{std::move(blob).error()};
    }
    // extract and return symlinks embedded in the Bazel Directory
    // message as separate blobs to upload them later
    std::unordered_set<ArtifactBlob> link_blobs;
    for (auto const& link : dir.symlinks()) {
        auto link_blob = ArtifactBlob::FromMemory(
            hash_function, ObjectType::File, link.target());
        if (not link_blob) {
            return unexpected{std::move(link_blob).error()};
        }
        link_blobs.insert(std::move(link_blob).value());
    }
    return std::make_tuple(
        std::move(blob).value(), std::move(link_blobs), has_upwards_symlinks);
}

}  // namespace

auto BazelResponse::ReadStringBlob(bazel_re::Digest const& id) noexcept
    -> std::string {
    auto digest = ArtifactDigestFactory::FromBazel(
        network_->GetHashFunction().GetType(), id);
    if (digest.has_value()) {
        auto reader = network_->CreateReader();
        if (auto blob = reader.ReadSingleBlob(*digest)) {
            if (auto const content = blob->ReadContent()) {
                return *content;
            }
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

auto BazelResponse::HasUpwardsSymlinks() noexcept
    -> expected<bool, std::string> {
    if (auto error_msg = Populate()) {
        return unexpected{*std::move(error_msg)};
    }
    return has_upwards_symlinks_;
}

auto BazelResponse::Populate() noexcept -> std::optional<std::string> {
    // Initialized only once lazily
    if (populated_) {
        return std::nullopt;
    }

    ArtifactInfos artifacts{};
    auto const& action_result = output_.action_result;
    artifacts.reserve(
        static_cast<std::size_t>(action_result.output_files_size()) +
        static_cast<std::size_t>(action_result.output_file_symlinks_size()) +
        static_cast<std::size_t>(
            action_result.output_directory_symlinks_size()) +
        static_cast<std::size_t>(action_result.output_directories_size()));

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

    // in compatible mode: collect all symlinks and upload them as separate
    // blobs
    std::unordered_set<ArtifactBlob> link_blobs;

    // collect all symlinks and store them
    for (auto const* v_ptr : {&action_result.output_symlinks(),
                              // DEPRECATED as of v2.1
                              &action_result.output_file_symlinks(),
                              // DEPRECATED as of v2.1
                              &action_result.output_directory_symlinks()}) {
        for (auto const& link : *v_ptr) {
            try {
                // in compatible mode: track upwards symlinks
                has_upwards_symlinks_ =
                    has_upwards_symlinks_ or
                    (not ProtocolTraits::IsNative(hash_type) and
                     not PathIsNonUpwards(link.target()));
                Artifact::ObjectInfo info;
                info.type = ObjectType::Symlink;
                if (ProtocolTraits::IsNative(hash_type)) {
                    info.digest =
                        ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                            network_->GetHashFunction(), link.target());
                }
                else {
                    auto link_blob =
                        ArtifactBlob::FromMemory(network_->GetHashFunction(),
                                                 ObjectType::File,
                                                 link.target());
                    if (not link_blob) {
                        return fmt::format(
                            "BazelResponse: failure during blob generation of "
                            "link {}:\n{}",
                            link.path(),
                            std::move(link_blob).error());
                    }
                    info.digest = link_blob->GetDigest();
                    link_blobs.insert(std::move(link_blob).value());
                }
                artifacts.emplace(link.path(), std::move(info));
            } catch (std::exception const& ex) {
                return fmt::format(
                    "BazelResponse: unexpected failure gathering digest for "
                    "{}:\n{}",
                    link.path(),
                    ex.what());
            }
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
        populated_ = true;
        return std::nullopt;
    }

    // Upload received symlinks.
    if (not network_->UploadBlobs(std::move(link_blobs))) {
        return fmt::format("BazelResponse: failure uploading link blobs\n");
    }

    // obtain tree digests for output directories
    std::vector<ArtifactDigest> tree_digests{};
    tree_digests.reserve(
        gsl::narrow<std::size_t>(action_result.output_directories_size()));
    for (auto const& directory : action_result.output_directories()) {
        auto digest = ArtifactDigestFactory::FromBazel(hash_type,
                                                       directory.tree_digest());
        if (not digest) {
            return std::move(digest).error();
        }
        tree_digests.push_back(*std::move(digest));
    }

    // collect root digests from trees and store them
    auto reader = network_->CreateReader();
    int pos = 0;
    for (auto const& tree_blob : reader.ReadOrdered(tree_digests)) {
        try {
            std::optional<bazel_re::Tree> tree;
            if (auto const content = tree_blob.ReadContent()) {
                tree = BazelMsgFactory::MessageFromString<bazel_re::Tree>(
                    *content);
            }
            if (not tree) {
                return fmt::format(
                    "BazelResponse: failed to create Tree for {}",
                    tree_blob.GetDigest().hash());
            }

            // The server does not store the Directory messages it just
            // has sent us as part of the Tree message. If we want to be
            // able to use the Directories as inputs for actions, we
            // have to upload them manually.
            auto upload_result = UploadTreeMessageDirectories(*tree);
            if (not upload_result) {
                auto error = fmt::format("BazelResponse: {}",
                                         std::move(upload_result).error());
                Logger::Log(LogLevel::Trace, error);
                return error;
            }
            auto [tree_digest, tree_has_upwards_symlinks] =
                std::move(upload_result).value();
            has_upwards_symlinks_ =
                has_upwards_symlinks_ or tree_has_upwards_symlinks;
            artifacts.emplace(
                action_result.output_directories(pos).path(),
                Artifact::ObjectInfo{.digest = std::move(tree_digest),
                                     .type = ObjectType::Tree});
        } catch (std::exception const& ex) {
            return fmt::format(
                "BazelResponse: unexpected failure gathering digest for "
                "{}:\n{}",
                tree_blob.GetDigest().hash(),
                ex.what());
        }
        ++pos;
    }
    artifacts_ = std::move(artifacts);
    populated_ = true;
    return std::nullopt;
}

auto BazelResponse::UploadTreeMessageDirectories(bazel_re::Tree const& tree)
    const -> expected<std::pair<ArtifactDigest, /*has_upwards_symlinks*/ bool>,
                      std::string> {
    auto const upload_callback =
        [&network =
             *network_](std::unordered_set<ArtifactBlob>&& blobs) -> bool {
        return network.UploadBlobs(std::move(blobs));
    };
    auto const hash_function = network_->GetHashFunction();

    auto rootdir_result = ProcessDirectoryMessage(hash_function, tree.root());
    if (not rootdir_result) {
        return unexpected{std::move(rootdir_result).error()};
    }
    auto [rootdir_blob, rootdir_link_blobs, has_upwards_symlinks] =
        std::move(rootdir_result).value();

    // Upload symlinks from root Directory message.
    auto rootdir_digest = rootdir_blob.GetDigest();
    std::unordered_set<ArtifactBlob> blobs{std::move(rootdir_blob)};
    for (auto link_blob : rootdir_link_blobs) {
        auto link_digest = link_blob.GetDigest();
        if (not UpdateContainerAndUpload(&blobs,
                                         std::move(link_blob),
                                         /*exception_is_fatal=*/false,
                                         upload_callback)) {
            return unexpected{fmt::format(
                "failed to upload blobs for the Tree with the root digest {}",
                rootdir_digest.hash())};
        }
    }

    for (auto const& subdir : tree.children()) {
        // store or upload blob, taking maximum transfer size into account
        auto subdir_result = ProcessDirectoryMessage(hash_function, subdir);
        if (not subdir_result) {
            return unexpected{std::move(subdir_result).error()};
        }
        auto [subdir_blob, subdir_link_blobs, subdir_upwards_symlinks] =
            std::move(subdir_result).value();
        has_upwards_symlinks = has_upwards_symlinks or subdir_upwards_symlinks;

        // Upload subdir Directory message.
        auto subdir_digest = subdir_blob.GetDigest();
        if (not UpdateContainerAndUpload(&blobs,
                                         std::move(subdir_blob),
                                         /*exception_is_fatal=*/false,
                                         upload_callback)) {
            return unexpected{fmt::format(
                "failed to upload blobs for the Tree with the root digest {}",
                rootdir_digest.hash())};
        }

        // Upload symlinks from root Directory message as explicit blobs.
        for (auto link_blob : subdir_link_blobs) {
            auto link_digest = link_blob.GetDigest();
            if (not UpdateContainerAndUpload(&blobs,
                                             std::move(link_blob),
                                             /*exception_is_fatal=*/false,
                                             upload_callback)) {
                return unexpected{
                    fmt::format("failed to upload blobs for the Tree with the "
                                "root digest {}",
                                subdir_digest.hash())};
            }
        }
    }

    // upload any remaining blob
    if (not std::invoke(upload_callback, std::move(blobs))) {
        return unexpected{fmt::format(
            "failed to upload blobs for the Tree with the root digest {}",
            rootdir_digest.hash())};
    }

    return std::make_pair(rootdir_digest, has_upwards_symlinks);
}
