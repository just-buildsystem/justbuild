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
#include <cstdio>
#include <exception>
#include <functional>
#include <iterator>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <utility>  // std::move

#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/content_blob_container.hpp"
#include "src/buildtool/execution_api/common/stream_dumper.hpp"
#include "src/buildtool/execution_api/common/tree_reader.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_action.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network_reader.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/utils/cpp/back_map.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/transformed_range.hpp"

namespace {

[[nodiscard]] auto RetrieveToCas(
    std::vector<ArtifactDigest> const& digests,
    IExecutionApi const& api,
    std::shared_ptr<BazelNetwork> const& network,
    std::unordered_map<ArtifactDigest, Artifact::ObjectInfo> const&
        info_map) noexcept -> bool {

    // Fetch blobs from this CAS.
    auto size = digests.size();
    auto reader = network->CreateReader();
    std::size_t count{};
    ArtifactBlobContainer container{};
    for (auto blobs : reader.ReadIncrementally(digests)) {
        if (count + blobs.size() > size) {
            Logger::Log(LogLevel::Warning,
                        "received more blobs than requested.");
            return false;
        }
        for (auto& blob : blobs) {
            blob.is_exec =
                info_map.contains(blob.digest)
                    ? IsExecutableObject(info_map.at(blob.digest).type)
                    : false;
            // Collect blob and upload to other CAS if transfer size reached.
            if (not UpdateContainerAndUpload<ArtifactDigest>(
                    &container,
                    std::move(blob),
                    /*exception_is_fatal=*/true,
                    [&api](ArtifactBlobContainer&& blobs) {
                        return api.Upload(std::move(blobs),
                                          /*skip_find_missing=*/true);
                    })) {
                return false;
            }
        }
        count += blobs.size();
    }

    if (count != size) {
        Logger::Log(LogLevel::Debug, "could not retrieve all requested blobs.");
        return false;
    }

    // Upload remaining blobs to other CAS.
    return api.Upload(std::move(container), /*skip_find_missing=*/true);
}

[[nodiscard]] auto RetrieveToCasSplitted(
    Artifact::ObjectInfo const& artifact_info,
    IExecutionApi const& this_api,
    IExecutionApi const& other_api,
    std::shared_ptr<BazelNetwork> const& network,
    std::unordered_map<ArtifactDigest, Artifact::ObjectInfo> const&
        info_map) noexcept -> bool {

    // Split blob into chunks at the remote side and retrieve chunk digests.
    auto chunk_digests = this_api.SplitBlob(artifact_info.digest);
    if (not chunk_digests) {
        // If blob splitting failed, fall back to regular fetching.
        return ::RetrieveToCas(
            {artifact_info.digest}, other_api, network, info_map);
    }

    // Fetch unknown chunks.
    auto missing_artifact_digests = other_api.GetMissingDigests(
        std::unordered_set(chunk_digests->begin(), chunk_digests->end()));

    std::vector<ArtifactDigest> missing_digests;
    missing_digests.reserve(missing_artifact_digests.size());
    std::move(missing_artifact_digests.begin(),
              missing_artifact_digests.end(),
              std::back_inserter(missing_digests));
    if (not ::RetrieveToCas(missing_digests, other_api, network, info_map)) {
        return false;
    }

    // Assemble blob from chunks.
    auto digest = other_api.SpliceBlob(artifact_info.digest, *chunk_digests);
    if (not digest) {
        // If blob splicing failed, fall back to regular fetching.
        return ::RetrieveToCas(
            {artifact_info.digest}, other_api, network, info_map);
    }
    return true;
}

[[nodiscard]] auto ConvertToBazelBlobContainer(
    ArtifactBlobContainer&& container) noexcept
    -> std::optional<std::unordered_set<BazelBlob>> {
    std::unordered_set<BazelBlob> blobs;
    try {
        blobs.reserve(container.Size());
        for (const auto& blob : container.Blobs()) {
            blobs.emplace(ArtifactDigestFactory::ToBazel(blob.digest),
                          blob.data,
                          blob.is_exec);
        }
    } catch (...) {
        return std::nullopt;
    }
    return blobs;
}

}  // namespace

BazelApi::BazelApi(
    std::string const& instance_name,
    std::string const& host,
    Port port,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RetryConfig const*> const& retry_config,
    ExecutionConfiguration const& exec_config,
    gsl::not_null<HashFunction const*> const& hash_function) noexcept {
    network_ = std::make_shared<BazelNetwork>(instance_name,
                                              host,
                                              port,
                                              auth,
                                              retry_config,
                                              exec_config,
                                              hash_function);
}

// implement move constructor in cpp, where all members are complete types
BazelApi::BazelApi(BazelApi&& other) noexcept = default;

// implement destructor in cpp, where all members are complete types
BazelApi::~BazelApi() = default;

auto BazelApi::CreateAction(
    ArtifactDigest const& root_digest,
    std::vector<std::string> const& command,
    std::string const& cwd,
    std::vector<std::string> const& output_files,
    std::vector<std::string> const& output_dirs,
    std::map<std::string, std::string> const& env_vars,
    std::map<std::string, std::string> const& properties) const noexcept
    -> IExecutionAction::Ptr {
    return std::unique_ptr<BazelAction>{new (std::nothrow)
                                            BazelAction{network_,
                                                        root_digest,
                                                        command,
                                                        cwd,
                                                        output_files,
                                                        output_dirs,
                                                        env_vars,
                                                        properties}};
}

// NOLINTNEXTLINE(google-default-arguments)
[[nodiscard]] auto BazelApi::RetrieveToPaths(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<std::filesystem::path> const& output_paths,
    IExecutionApi const* alternative) const noexcept -> bool {
    if (artifacts_info.size() != output_paths.size()) {
        Logger::Log(LogLevel::Warning,
                    "different number of digests and output paths.");
        return false;
    }

    // Obtain file digests from artifact infos
    std::vector<ArtifactDigest> file_digests{};
    std::vector<std::size_t> artifact_pos{};
    for (std::size_t i{}; i < artifacts_info.size(); ++i) {
        auto const& info = artifacts_info[i];
        if (alternative != nullptr and alternative != this and
            alternative->IsAvailable(info.digest)) {
            if (not alternative->RetrieveToPaths({info}, {output_paths[i]})) {
                return false;
            }
        }
        else {
            if (IsTreeObject(info.type)) {
                // read object infos from sub tree and call retrieve recursively
                auto request_remote_tree = alternative != nullptr
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
    auto reader = network_->CreateReader();
    std::size_t count{};
    for (auto blobs : reader.ReadIncrementally(file_digests)) {
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
    }

    if (count != size) {
        Logger::Log(LogLevel::Warning,
                    "could not retrieve all requested blobs.");
        return false;
    }

    return true;
}

// NOLINTNEXTLINE(google-default-arguments)
[[nodiscard]] auto BazelApi::RetrieveToFds(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<int> const& fds,
    bool raw_tree,
    IExecutionApi const* alternative) const noexcept -> bool {
    if (alternative == nullptr or alternative == this) {
        auto dumper =
            StreamDumper<BazelNetworkReader>{network_->CreateReader()};
        return CommonRetrieveToFds(
            artifacts_info,
            fds,
            [&dumper, &raw_tree](Artifact::ObjectInfo const& info,
                                 gsl::not_null<FILE*> const& out) {
                return dumper.DumpToStream(info, out, raw_tree);
            },
            std::nullopt  // no fallback
        );
    }

    // We have an alternative, and, in fact, preferred API. So go
    // through the artifacts one by one and first try the the preferred one,
    // then fall back to retrieving ourselves.
    if (artifacts_info.size() != fds.size()) {
        Logger::Log(LogLevel::Error,
                    "different number of digests and file descriptors.");
        return false;
    }
    for (std::size_t i{}; i < artifacts_info.size(); ++i) {
        auto fd = fds[i];
        auto const& info = artifacts_info[i];
        if (alternative->IsAvailable(info.digest)) {
            if (not alternative->RetrieveToFds(
                    std::vector<Artifact::ObjectInfo>{info},
                    std::vector<int>{fd},
                    raw_tree,
                    nullptr)) {
                return false;
            }
        }
        else {
            if (not RetrieveToFds(std::vector<Artifact::ObjectInfo>{info},
                                  std::vector<int>{fd},
                                  raw_tree,
                                  nullptr)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto BazelApi::RetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    IExecutionApi const& api) const noexcept -> bool {
    // Return immediately if target CAS is this CAS
    if (this == &api) {
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
    std::vector<ArtifactDigest> blob_digests{};
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

[[nodiscard]] auto BazelApi::ParallelRetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    IExecutionApi const& api,
    std::size_t jobs,
    bool use_blob_splitting) const noexcept -> bool {
    // Return immediately if target CAS is this CAS
    if (this == &api) {
        return true;
    }
    std::unordered_set<Artifact::ObjectInfo> done{};
    return ParallelRetrieveToCasWithCache(
        artifacts_info, api, jobs, use_blob_splitting, &done);
}

[[nodiscard]] auto BazelApi::ParallelRetrieveToCasWithCache(
    std::vector<Artifact::ObjectInfo> const& all_artifacts_info,
    IExecutionApi const& api,
    std::size_t jobs,
    bool use_blob_splitting,
    gsl::not_null<std::unordered_set<Artifact::ObjectInfo>*> done)
    const noexcept -> bool {
    std::unordered_set<Artifact::ObjectInfo> artifacts_info;
    try {
        artifacts_info.reserve(all_artifacts_info.size());
        for (auto const& info : all_artifacts_info) {
            if (not done->contains(info)) {
                artifacts_info.emplace(info);
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error,
            "BazelApi: Collecting the set of artifacts failed with:\n{}",
            ex.what());
        return false;
    }
    if (artifacts_info.empty()) {
        return true;  // Nothing to do
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
    std::vector<Artifact::ObjectInfo> prerequisites{};
    std::mutex prerequisites_lock{};
    try {
        auto ts = TaskSystem{jobs};
        for (auto const& dgst : missing_artifacts_info->digests) {
            auto const& info = missing_artifacts_info->back_map[dgst];
            if (IsTreeObject(info.type)) {
                ts.QueueTask([this,
                              &info,
                              &failure,
                              &prerequisites,
                              &prerequisites_lock]() {
                    auto reader = TreeReader<BazelNetworkReader>{
                        network_->CreateReader()};
                    auto const result = reader.ReadDirectTreeEntries(
                        info.digest, std::filesystem::path{});
                    if (not result) {
                        failure = true;
                        return;
                    }
                    {
                        std::unique_lock lock{prerequisites_lock};
                        prerequisites.insert(prerequisites.end(),
                                             result->infos.begin(),
                                             result->infos.end());
                    }
                });
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Warning,
                    "Artifact synchronization failed: {}",
                    ex.what());
        return false;
    }

    if (failure) {
        return false;
    }

    if (not ParallelRetrieveToCasWithCache(
            prerequisites, api, jobs, use_blob_splitting, done)) {
        return false;
    }

    // In parallel process all the requested artifacts
    try {
        auto ts = TaskSystem{jobs};
        for (auto const& dgst : missing_artifacts_info->digests) {
            auto const& info = missing_artifacts_info->back_map[dgst];
            ts.QueueTask([this,
                          &info,
                          &api,
                          &failure,
                          &info_map = missing_artifacts_info->back_map,
                          use_blob_splitting]() {
                if (use_blob_splitting and network_->BlobSplitSupport() and
                            api.BlobSpliceSupport()
                        ? ::RetrieveToCasSplitted(
                              info, *this, api, network_, info_map)
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

    try {
        for (auto const& info : artifacts_info) {
            done->insert(info);
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Warning,
                    "Exception when updating set of synchronized objects "
                    "(continuing anyway): {}",
                    ex.what());
    }

    return not failure;
}

[[nodiscard]] auto BazelApi::RetrieveToMemory(
    Artifact::ObjectInfo const& artifact_info) const noexcept
    -> std::optional<std::string> {
    auto reader = network_->CreateReader();
    if (auto blob = reader.ReadSingleBlob(artifact_info.digest)) {
        return *blob->data;
    }
    return std::nullopt;
}

[[nodiscard]] auto BazelApi::Upload(ArtifactBlobContainer&& blobs,
                                    bool skip_find_missing) const noexcept
    -> bool {
    auto bazel_blobs = ConvertToBazelBlobContainer(std::move(blobs));
    return bazel_blobs ? network_->UploadBlobs(*std::move(bazel_blobs),
                                               skip_find_missing)
                       : false;
}

[[nodiscard]] auto BazelApi::UploadTree(
    std::vector<DependencyGraph::NamedArtifactNodePtr> const& artifacts)
    const noexcept -> std::optional<ArtifactDigest> {
    auto build_root = DirectoryTree::FromNamedArtifacts(artifacts);
    if (not build_root) {
        Logger::Log(LogLevel::Debug,
                    "failed to create build root from artifacts.");
        return std::nullopt;
    }

    if (ProtocolTraits::IsNative(network_->GetHashFunction().GetType())) {
        return CommonUploadTreeNative(*this, *build_root);
    }
    return CommonUploadTreeCompatible(
        *this,
        *build_root,
        [&network = network_](
            std::vector<ArtifactDigest> const& digests,
            gsl::not_null<std::vector<std::string>*> const& targets) {
            auto reader = network->CreateReader();
            targets->reserve(digests.size());
            for (auto blobs : reader.ReadIncrementally(digests)) {
                for (auto const& blob : blobs) {
                    targets->emplace_back(*blob.data);
                }
            }
        });
}

[[nodiscard]] auto BazelApi::IsAvailable(
    ArtifactDigest const& digest) const noexcept -> bool {
    return network_->IsAvailable(ArtifactDigestFactory::ToBazel(digest));
}

[[nodiscard]] auto BazelApi::GetMissingDigests(
    std::unordered_set<ArtifactDigest> const& digests) const noexcept
    -> std::unordered_set<ArtifactDigest> {
    auto const back_map = BackMap<bazel_re::Digest, ArtifactDigest>::Make(
        &digests, ArtifactDigestFactory::ToBazel);
    if (not back_map.has_value()) {
        return digests;
    }

    auto const bazel_result = network_->FindMissingBlobs(back_map->GetKeys());
    return back_map->GetValues(bazel_result);
}

[[nodiscard]] auto BazelApi::SplitBlob(ArtifactDigest const& blob_digest)
    const noexcept -> std::optional<std::vector<ArtifactDigest>> {
    auto const chunk_digests =
        network_->SplitBlob(ArtifactDigestFactory::ToBazel(blob_digest));
    if (not chunk_digests) {
        return std::nullopt;
    }
    auto artifact_digests = std::vector<ArtifactDigest>{};
    artifact_digests.reserve(chunk_digests->size());
    for (auto const& chunk : *chunk_digests) {
        auto part = ArtifactDigestFactory::FromBazel(
            network_->GetHashFunction().GetType(), chunk);
        if (not part) {
            return std::nullopt;
        }
        artifact_digests.emplace_back(*std::move(part));
    }
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
                       return ArtifactDigestFactory::ToBazel(artifact_digest);
                   });
    auto const digest = network_->SpliceBlob(
        ArtifactDigestFactory::ToBazel(blob_digest), digests);
    if (not digest) {
        return std::nullopt;
    }
    auto result = ArtifactDigestFactory::FromBazel(
        network_->GetHashFunction().GetType(), *digest);
    if (not result) {
        return std::nullopt;
    }
    return *std::move(result);
}

[[nodiscard]] auto BazelApi::BlobSpliceSupport() const noexcept -> bool {
    return network_->BlobSpliceSupport();
}
