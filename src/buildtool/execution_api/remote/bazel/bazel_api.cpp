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

#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
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
    network_ =
        std::make_shared<BazelNetwork>(instance_name, host, port, exec_config);
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
                                                        root_digest,
                                                        command,
                                                        output_files,
                                                        output_dirs,
                                                        env_vars,
                                                        properties}};
}

// NOLINTNEXTLINE(misc-no-recursion, google-default-arguments)
[[nodiscard]] auto BazelApi::RetrieveToPaths(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<std::filesystem::path> const& output_paths,
    IExecutionApi* alternative) noexcept -> bool {
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
        if ((alternative != nullptr) and
            alternative->IsAvailable(info.digest)) {
            if (not alternative->RetrieveToPaths({info}, {output_paths[i]})) {
                return false;
            }
        }
        else {
            if (IsTreeObject(info.type)) {
                // read object infos from sub tree and call retrieve recursively
                auto const infos = network_->RecursivelyReadTreeLeafs(
                    info.digest, output_paths[i], alternative != nullptr);
                if (not infos or
                    not RetrieveToPaths(infos->second, infos->first)) {
                    return false;
                }
            }
            else {
                file_digests.emplace_back(info.digest);
                artifact_pos.emplace_back(i);
            }
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
            if (not FileSystemManager::WriteFileAs</*kSetEpochTime=*/true,
                                                   /*kSetWritable=*/true>(
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
    std::vector<int> const& fds,
    bool /*raw_tree*/) noexcept -> bool {
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

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto BazelApi::RetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    gsl::not_null<IExecutionApi*> const& api) noexcept -> bool {

    // Return immediately if target CAS is this CAS
    if (this == api) {
        return true;
    }

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

    // Recursively process trees.
    std::vector<bazel_re::Digest> blob_digests{};
    for (auto const& info : missing_artifacts_info) {
        if (IsTreeObject(info.type)) {
            auto const infos = network_->ReadDirectTreeEntries(
                info.digest, std::filesystem::path{});
            if (not infos or not RetrieveToCas(infos->second, api)) {
                return false;
            }
        }

        // Object infos created by network_->ReadTreeInfos() will contain 0 as
        // size, but this is handled by the remote execution engine, so no need
        // to regenerate the digest.
        blob_digests.push_back(info.digest);
    }

    // Fetch blobs from this CAS.
    auto size = blob_digests.size();
    auto reader = network_->ReadBlobs(std::move(blob_digests));
    auto blobs = reader.Next();
    std::size_t count{};
    BlobContainer container{};
    while (not blobs.empty()) {
        if (count + blobs.size() > size) {
            Logger::Log(LogLevel::Error, "received more blobs than requested.");
            return false;
        }
        for (auto& blob : blobs) {
            try {
                auto exec = IsExecutableObject(
                    info_map[ArtifactDigest{blob.digest}].type);
                container.Emplace(BazelBlob{blob.digest, blob.data, exec});
            } catch (std::exception const& ex) {
                Logger::Log(
                    LogLevel::Error, "failed to emplace blob: ", ex.what());
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

    // Upload blobs to other CAS.
    return api->Upload(container, /*skip_find_missing=*/true);
}

[[nodiscard]] auto BazelApi::Upload(BlobContainer const& blobs,
                                    bool skip_find_missing) noexcept -> bool {
    return network_->UploadBlobs(blobs, skip_find_missing);
}

/// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto BazelApi::UploadBlobTree(
    BlobTreePtr const& blob_tree) noexcept -> bool {

    // Create digest list from blobs for batch availability check.
    std::vector<bazel_re::Digest> digests;
    digests.reserve(blob_tree->size());
    std::unordered_map<bazel_re::Digest, BlobTreePtr> tree_map;
    for (auto const& node : *blob_tree) {
        auto digest = node->Blob().digest;
        digests.emplace_back(digest);
        try {
            tree_map.emplace(std::move(digest), node);
        } catch (...) {
            return false;
        }
    }

    // Find missing digests.
    auto missing_digests = network_->IsAvailable(digests);

    // Process missing blobs.
    BlobContainer container;
    for (auto const& digest : missing_digests) {
        if (auto it = tree_map.find(digest); it != tree_map.end()) {
            auto const& node = it->second;
            // Process trees.
            if (node->IsTree()) {
                if (not UploadBlobTree(node)) {
                    return false;
                }
            }
            // Store blob.
            try {
                container.Emplace(node->Blob());
            } catch (...) {
                return false;
            }
        }
    }

    return network_->UploadBlobs(container, /*skip_find_missing=*/true);
}

[[nodiscard]] auto BazelApi::UploadTree(
    std::vector<DependencyGraph::NamedArtifactNodePtr> const&
        artifacts) noexcept -> std::optional<ArtifactDigest> {
    auto build_root = DirectoryTree::FromNamedArtifacts(artifacts);
    if (not build_root) {
        Logger::Log(LogLevel::Debug,
                    "failed to create build root from artifacts.");
        return std::nullopt;
    }

    if (Compatibility::IsCompatible()) {
        BlobContainer blobs{};
        auto digest = BazelMsgFactory::CreateDirectoryDigestFromTree(
            *build_root,
            [&blobs](BazelBlob&& blob) { blobs.Emplace(std::move(blob)); });
        if (not digest) {
            Logger::Log(LogLevel::Debug,
                        "failed to create digest for build root.");
            return std::nullopt;
        }
        Logger::Log(LogLevel::Trace, [&digest]() {
            std::ostringstream oss{};
            oss << "upload root directory" << std::endl;
            oss << fmt::format(" - root digest: {}", digest->hash())
                << std::endl;
            return oss.str();
        });
        if (not Upload(blobs, /*skip_find_missing=*/false)) {
            Logger::Log(LogLevel::Debug,
                        "failed to upload blobs for build root.");
            return std::nullopt;
        }
        return ArtifactDigest{*digest};
    }

    auto blob_tree = BlobTree::FromDirectoryTree(*build_root);
    if (not blob_tree) {
        Logger::Log(LogLevel::Debug,
                    "failed to create blob tree for build root.");
        return std::nullopt;
    }
    auto tree_blob = (*blob_tree)->Blob();
    // Upload blob tree if tree is not available at the remote side (content
    // first).
    if (not network_->IsAvailable(tree_blob.digest)) {
        if (not UploadBlobTree(*blob_tree)) {
            Logger::Log(LogLevel::Debug,
                        "failed to upload blob tree for build root.");
            return std::nullopt;
        }
        if (not Upload(BlobContainer{{tree_blob}},
                       /*skip_find_missing=*/true)) {
            Logger::Log(LogLevel::Debug,
                        "failed to upload tree blob for build root.");
            return std::nullopt;
        }
    }
    return ArtifactDigest{tree_blob.digest};
}

[[nodiscard]] auto BazelApi::IsAvailable(
    ArtifactDigest const& digest) const noexcept -> bool {
    return network_->IsAvailable(digest);
}

[[nodiscard]] auto BazelApi::IsAvailable(
    std::vector<ArtifactDigest> const& digests) const noexcept
    -> std::vector<ArtifactDigest> {
    std::vector<bazel_re::Digest> bazel_digests;
    bazel_digests.reserve(digests.size());
    std::unordered_map<bazel_re::Digest, ArtifactDigest> digest_map;
    for (auto const& digest : digests) {
        auto const& bazel_digest = static_cast<bazel_re::Digest>(digest);
        bazel_digests.push_back(bazel_digest);
        digest_map[bazel_digest] = digest;
    }
    auto bazel_result = network_->IsAvailable(bazel_digests);
    std::vector<ArtifactDigest> result;
    result.reserve(bazel_result.size());
    for (auto const& bazel_digest : bazel_result) {
        result.push_back(digest_map[bazel_digest]);
    }
    return result;
}
