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
#include <atomic>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>  // std::move

#include "fmt/core.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/stream_dumper.hpp"
#include "src/buildtool/execution_api/common/tree_reader.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_ac_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_action.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network_reader.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_response.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/fs_utils.hpp"

namespace {

[[nodiscard]] auto RetrieveToCas(
    std::vector<bazel_re::Digest> const& digests,
    gsl::not_null<IExecutionApi*> const& api,
    std::shared_ptr<BazelNetwork> const& network,
    std::unordered_map<ArtifactDigest, Artifact::ObjectInfo> const&
        info_map) noexcept -> bool {

    // Fetch blobs from this CAS.
    auto size = digests.size();
    auto reader = network->ReadBlobs(digests);
    auto blobs = reader.Next();
    std::size_t count{};
    ArtifactBlobContainer container{};
    while (not blobs.empty()) {
        if (count + blobs.size() > size) {
            Logger::Log(LogLevel::Warning,
                        "received more blobs than requested.");
            return false;
        }
        for (auto const& blob : blobs) {
            auto digest = ArtifactDigest{blob.digest};
            auto exec = info_map.contains(digest)
                            ? IsExecutableObject(info_map.at(digest).type)
                            : false;
            // Collect blob and upload to other CAS if transfer size reached.
            if (not UpdateContainerAndUpload<ArtifactDigest>(
                    &container,
                    ArtifactBlob{std::move(digest), blob.data, exec},
                    /*exception_is_fatal=*/true,
                    [&api](ArtifactBlobContainer&& blobs) {
                        return api->Upload(std::move(blobs),
                                           /*skip_find_missing=*/true);
                    })) {
                return false;
            }
        }
        count += blobs.size();
        blobs = reader.Next();
    }

    if (count != size) {
        Logger::Log(LogLevel::Debug, "could not retrieve all requested blobs.");
        return false;
    }

    // Upload remaining blobs to other CAS.
    return api->Upload(std::move(container), /*skip_find_missing=*/true);
}

[[nodiscard]] auto RetrieveToCasSplitted(
    Artifact::ObjectInfo const& artifact_info,
    gsl::not_null<IExecutionApi*> const& this_api,
    gsl::not_null<IExecutionApi*> const& other_api,
    std::shared_ptr<BazelNetwork> const& network,
    std::unordered_map<ArtifactDigest, Artifact::ObjectInfo> const&
        info_map) noexcept -> bool {

    // Split blob into chunks at the remote side and retrieve chunk digests.
    auto chunk_digests = this_api->SplitBlob(artifact_info.digest);
    if (not chunk_digests) {
        // If blob splitting failed, fall back to regular fetching.
        return ::RetrieveToCas(
            {artifact_info.digest}, other_api, network, info_map);
    }

    // Fetch unknown chunks.
    auto digest_set = std::unordered_set<ArtifactDigest>{chunk_digests->begin(),
                                                         chunk_digests->end()};
    auto unique_digests =
        std::vector<ArtifactDigest>{digest_set.begin(), digest_set.end()};

    auto missing_artifact_digests = other_api->IsAvailable(unique_digests);

    auto missing_digests = std::vector<bazel_re::Digest>{};
    missing_digests.reserve(digest_set.size());
    std::transform(missing_artifact_digests.begin(),
                   missing_artifact_digests.end(),
                   std::back_inserter(missing_digests),
                   [](auto const& digest) {
                       return static_cast<bazel_re::Digest>(digest);
                   });
    if (not ::RetrieveToCas(missing_digests, other_api, network, info_map)) {
        return false;
    }

    // Assemble blob from chunks.
    auto digest = other_api->SpliceBlob(artifact_info.digest, *chunk_digests);
    if (not digest) {
        // If blob splicing failed, fall back to regular fetching.
        return ::RetrieveToCas(
            {artifact_info.digest}, other_api, network, info_map);
    }

    Logger::Log(
        LogLevel::Debug,
        [&artifact_info,
         &unique_digests,
         &missing_artifact_digests,
         total_size = digest->size()]() {
            auto missing_digest_set = std::unordered_set<ArtifactDigest>{
                missing_artifact_digests.begin(),
                missing_artifact_digests.end()};
            std::uint64_t transmitted_bytes{0};
            for (auto const& chunk_digest : unique_digests) {
                if (missing_digest_set.contains(chunk_digest)) {
                    transmitted_bytes += chunk_digest.size();
                }
            }
            double transmission_factor =
                (total_size > 0) ? 100.0 * transmitted_bytes / total_size
                                 : 100.0;
            return fmt::format(
                "Blob splitting saved {} bytes ({:.2f}%) of network traffic "
                "when fetching {}.\n",
                total_size - transmitted_bytes,
                100.0 - transmission_factor,
                artifact_info.ToString());
        });

    return true;
}

[[nodiscard]] auto ConvertToBazelBlobContainer(
    ArtifactBlobContainer&& container) noexcept
    -> std::optional<BazelBlobContainer> {
    std::vector<BazelBlob> blobs;
    try {
        blobs.reserve(container.Size());
        for (const auto& blob : container.Blobs()) {
            blobs.emplace_back(blob.digest, blob.data, blob.is_exec);
        }
    } catch (...) {
        return std::nullopt;
    }
    return BazelBlobContainer{std::move(blobs)};
};

}  // namespace

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
    std::optional<gsl::not_null<IExecutionApi*>> const& alternative) noexcept
    -> bool {
    if (artifacts_info.size() != output_paths.size()) {
        Logger::Log(LogLevel::Warning,
                    "different number of digests and output paths.");
        return false;
    }

    // Obtain file digests from artifact infos
    std::vector<bazel_re::Digest> file_digests{};
    std::vector<std::size_t> artifact_pos{};
    for (std::size_t i{}; i < artifacts_info.size(); ++i) {
        auto const& info = artifacts_info[i];
        if (alternative and alternative.value()->IsAvailable(info.digest)) {
            if (not alternative.value()->RetrieveToPaths({info},
                                                         {output_paths[i]})) {
                return false;
            }
        }
        else {
            if (IsTreeObject(info.type)) {
                // read object infos from sub tree and call retrieve recursively
                auto request_remote_tree = alternative.has_value()
                                               ? std::make_optional(info.digest)
                                               : std::nullopt;
                auto reader = TreeReader<BazelNetworkReader>{
                    network_->CreateReader(), std::move(request_remote_tree)};
                auto const result = reader.RecursivelyReadTreeLeafs(
                    info.digest, output_paths[i]);
                if (not result or
                    not RetrieveToPaths(result->infos, result->paths)) {
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
            Logger::Log(LogLevel::Warning,
                        "received more blobs than requested.");
            return false;
        }
        for (std::size_t pos = 0; pos < blobs.size(); ++pos) {
            auto gpos = artifact_pos[count + pos];
            auto const& type = artifacts_info[gpos].type;
            if (not FileSystemManager::WriteFileAs</*kSetEpochTime=*/true,
                                                   /*kSetWritable=*/true>(
                    *blobs[pos].data, output_paths[gpos], type)) {
                Logger::Log(LogLevel::Warning,
                            "staging to output path {} failed.",
                            output_paths[gpos].string());
                return false;
            }
        }
        count += blobs.size();
        blobs = reader.Next();
    }

    if (count != size) {
        Logger::Log(LogLevel::Warning,
                    "could not retrieve all requested blobs.");
        return false;
    }

    return true;
}

[[nodiscard]] auto BazelApi::RetrieveToFds(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<int> const& fds,
    bool raw_tree) noexcept -> bool {
    auto dumper = StreamDumper<BazelNetworkReader>{network_->CreateReader()};
    return CommonRetrieveToFds(
        artifacts_info,
        fds,
        [&dumper, &raw_tree](Artifact::ObjectInfo const& info,
                             gsl::not_null<FILE*> const& out) {
            return dumper.DumpToStream(info, out, raw_tree);
        },
        std::nullopt  // remote can't fallback to Git
    );
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
    auto missing_artifacts_info = GetMissingArtifactsInfo<Artifact::ObjectInfo>(
        api,
        artifacts_info.begin(),
        artifacts_info.end(),
        [](Artifact::ObjectInfo const& info) { return info.digest; });
    if (not missing_artifacts_info) {
        Logger::Log(LogLevel::Error,
                    "BazelApi: Failed to retrieve the missing artifacts");
        return false;
    }

    // Recursively process trees.
    std::vector<bazel_re::Digest> blob_digests{};
    for (auto const& dgst : missing_artifacts_info->digests) {
        auto const& info = missing_artifacts_info->back_map[dgst];
        if (IsTreeObject(info.type)) {
            auto reader =
                TreeReader<BazelNetworkReader>{network_->CreateReader()};
            auto const result = reader.ReadDirectTreeEntries(
                info.digest, std::filesystem::path{});
            if (not result or not RetrieveToCas(result->infos, api)) {
                return false;
            }
        }

        // Object infos created by network_->ReadTreeInfos() will contain 0 as
        // size, but this is handled by the remote execution engine, so no need
        // to regenerate the digest.
        blob_digests.push_back(info.digest);
    }

    return ::RetrieveToCas(
        blob_digests, api, network_, missing_artifacts_info->back_map);
}

/// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto BazelApi::ParallelRetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    gsl::not_null<IExecutionApi*> const& api,
    std::size_t jobs,
    bool use_blob_splitting) noexcept -> bool {

    // Return immediately if target CAS is this CAS
    if (this == api) {
        return true;
    }

    // Determine missing artifacts in other CAS.
    auto missing_artifacts_info = GetMissingArtifactsInfo<Artifact::ObjectInfo>(
        api,
        artifacts_info.begin(),
        artifacts_info.end(),
        [](Artifact::ObjectInfo const& info) { return info.digest; });
    if (not missing_artifacts_info) {
        Logger::Log(LogLevel::Error,
                    "BazelApi: Failed to retrieve the missing artifacts");
        return false;
    }

    // Recursively process trees.
    std::atomic_bool failure{false};
    try {
        auto ts = TaskSystem{jobs};
        for (auto const& dgst : missing_artifacts_info->digests) {
            auto const& info = missing_artifacts_info->back_map[dgst];
            if (IsTreeObject(info.type)) {
                auto reader =
                    TreeReader<BazelNetworkReader>{network_->CreateReader()};
                auto const result = reader.ReadDirectTreeEntries(
                    info.digest, std::filesystem::path{});
                if (not result or
                    not ParallelRetrieveToCas(
                        result->infos, api, jobs, use_blob_splitting)) {
                    return false;
                }
            }

            // Object infos created by network_->ReadTreeInfos() will contain 0
            // as size, but this is handled by the remote execution engine, so
            // no need to regenerate the digest.
            ts.QueueTask([this,
                          &info,
                          &api,
                          &failure,
                          &info_map = missing_artifacts_info->back_map,
                          use_blob_splitting]() {
                if (use_blob_splitting and network_->BlobSplitSupport() and
                            api->BlobSpliceSupport()
                        ? ::RetrieveToCasSplitted(
                              info, this, api, network_, info_map)
                        : ::RetrieveToCas(
                              {info.digest}, api, network_, info_map)) {
                    return;
                }
                failure = true;
            });
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Warning,
                    "Artifact synchronization failed: {}",
                    ex.what());
        return false;
    }

    return not failure;
}

[[nodiscard]] auto BazelApi::RetrieveToMemory(
    Artifact::ObjectInfo const& artifact_info) noexcept
    -> std::optional<std::string> {
    auto blobs = network_->ReadBlobs({artifact_info.digest}).Next();
    if (blobs.size() == 1) {
        return *blobs.at(0).data;
    }
    return std::nullopt;
}

[[nodiscard]] auto BazelApi::Upload(ArtifactBlobContainer&& blobs,
                                    bool skip_find_missing) noexcept -> bool {
    auto bazel_blobs = ConvertToBazelBlobContainer(std::move(blobs));
    return bazel_blobs ? network_->UploadBlobs(std::move(*bazel_blobs),
                                               skip_find_missing)
                       : false;
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
        return CommonUploadTreeCompatible(
            this,
            *build_root,
            [&network = network_](std::vector<bazel_re::Digest> const& digests,
                                  std::vector<std::string>* targets) {
                auto reader = network->ReadBlobs(digests);
                auto blobs = reader.Next();
                targets->reserve(digests.size());
                while (not blobs.empty()) {
                    for (auto const& blob : blobs) {
                        targets->emplace_back(*blob.data);
                    }
                    blobs = reader.Next();
                }
            });
    }

    return CommonUploadTreeNative(this, *build_root);
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

[[nodiscard]] auto BazelApi::SplitBlob(ArtifactDigest const& blob_digest)
    const noexcept -> std::optional<std::vector<ArtifactDigest>> {
    auto chunk_digests =
        network_->SplitBlob(static_cast<bazel_re::Digest>(blob_digest));
    if (not chunk_digests) {
        return std::nullopt;
    }
    auto artifact_digests = std::vector<ArtifactDigest>{};
    artifact_digests.reserve(chunk_digests->size());
    std::transform(chunk_digests->cbegin(),
                   chunk_digests->cend(),
                   std::back_inserter(artifact_digests),
                   [](auto const& digest) { return ArtifactDigest{digest}; });
    return artifact_digests;
}

[[nodiscard]] auto BazelApi::BlobSplitSupport() const noexcept -> bool {
    return network_->BlobSplitSupport();
}

[[nodiscard]] auto BazelApi::SpliceBlob(
    ArtifactDigest const& blob_digest,
    std::vector<ArtifactDigest> const& chunk_digests) const noexcept
    -> std::optional<ArtifactDigest> {
    auto digests = std::vector<bazel_re::Digest>{};
    digests.reserve(chunk_digests.size());
    std::transform(chunk_digests.cbegin(),
                   chunk_digests.cend(),
                   std::back_inserter(digests),
                   [](auto const& artifact_digest) {
                       return static_cast<bazel_re::Digest>(artifact_digest);
                   });
    auto digest = network_->SpliceBlob(
        static_cast<bazel_re::Digest>(blob_digest), digests);
    if (not digest) {
        return std::nullopt;
    }
    return ArtifactDigest{*digest};
}

[[nodiscard]] auto BazelApi::BlobSpliceSupport() const noexcept -> bool {
    return network_->BlobSpliceSupport();
}
