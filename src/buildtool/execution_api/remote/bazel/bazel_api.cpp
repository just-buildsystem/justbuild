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
#include <compare>
#include <cstdio>
#include <exception>
#include <functional>
#include <iterator>
#include <mutex>
#include <new>
#include <unordered_set>
#include <utility>  // std::move

#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_api/bazel_msg/execution_config.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/stream_dumper.hpp"
#include "src/buildtool/execution_api/common/tree_reader.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_action.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_capabilities_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network_reader.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/utils/cpp/back_map.hpp"
#include "src/utils/cpp/expected.hpp"

namespace {

auto constexpr kVersion2dot1 =
    Capabilities::Version{.major = 2, .minor = 1, .patch = 0};

[[nodiscard]] auto RetrieveToCas(
    std::unordered_set<Artifact::ObjectInfo> const& infos,
    IExecutionApi const& api,
    std::shared_ptr<BazelNetwork> const& network) noexcept -> bool {
    auto const back_map = BackMap<ArtifactDigest, Artifact::ObjectInfo>::Make(
        &infos, [](Artifact::ObjectInfo const& info) { return info.digest; });
    if (back_map == nullptr) {
        return false;
    }

    // Fetch blobs from this CAS:
    auto reader = network->CreateReader();
    auto read_blobs = reader.Read(back_map->GetKeys());

    // Restore executable permissions:
    std::unordered_set<ArtifactBlob> result;
    result.reserve(read_blobs.size());
    for (auto it = read_blobs.begin(); it != read_blobs.end();) {
        auto const info = back_map->GetReference(it->GetDigest());

        auto node = read_blobs.extract(it++);
        node.value().SetExecutable(info.has_value() and
                                   IsExecutableObject(info.value()->type));
        result.insert(std::move(node));
    }

    auto const all_fetched = result.size() == infos.size();
    if (not all_fetched) {
        Logger::Log(LogLevel::Debug, "could not retrieve all requested blobs.");
    }
    // Upload blobs to other CAS.
    return api.Upload(std::move(result), /*skip_find_missing=*/true) and
           all_fetched;
}

[[nodiscard]] auto RetrieveToCasSplitted(
    Artifact::ObjectInfo const& artifact_info,
    IExecutionApi const& this_api,
    IExecutionApi const& other_api,
    std::shared_ptr<BazelNetwork> const& network) noexcept -> bool {
    // Split blob into chunks at the remote side and retrieve chunk digests.
    auto chunk_digests = this_api.SplitBlob(artifact_info.digest);
    if (not chunk_digests) {
        // If blob splitting failed, fall back to regular fetching.
        return ::RetrieveToCas({artifact_info}, other_api, network);
    }

    // Fetch unknown chunks.
    std::unordered_set<Artifact::ObjectInfo> missing;
    missing.reserve(chunk_digests->size());
    for (auto const& digest : other_api.GetMissingDigests(std::unordered_set(
             chunk_digests->begin(), chunk_digests->end()))) {
        missing.emplace(
            Artifact::ObjectInfo{digest,
                                 ObjectType::File,  // Chunks are always files
                                 /*failed=*/false});
    }

    if (not ::RetrieveToCas(missing, other_api, network)) {
        return false;
    }

    // Assemble blob from chunks.
    auto digest = other_api.SpliceBlob(artifact_info.digest, *chunk_digests);
    if (not digest) {
        // If blob splicing failed, fall back to regular fetching.
        return ::RetrieveToCas({artifact_info}, other_api, network);
    }
    return true;
}

}  // namespace

BazelApi::BazelApi(std::string const& instance_name,
                   std::string const& host,
                   Port port,
                   gsl::not_null<Auth const*> const& auth,
                   gsl::not_null<RetryConfig const*> const& retry_config,
                   ExecutionConfiguration const& exec_config,
                   HashFunction hash_function,
                   TmpDir::Ptr temp_space) noexcept {
    network_ = std::make_shared<BazelNetwork>(instance_name,
                                              host,
                                              port,
                                              auth,
                                              retry_config,
                                              exec_config,
                                              hash_function,
                                              std::move(temp_space));
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
    std::map<std::string, std::string> const& properties,
    bool force_legacy) const noexcept -> IExecutionAction::Ptr {
    if (ProtocolTraits::IsNative(GetHashType())) {
        // fall back to legacy for native
        force_legacy = true;
    }

    bool best_effort = not force_legacy;
    if (not force_legacy and
        network_->GetCapabilities()->high_api_version < kVersion2dot1) {
        best_effort = false;
    }
    if (not best_effort and
        network_->GetCapabilities()->low_api_version >= kVersion2dot1) {
        Logger::Log(LogLevel::Warning,
                    "Server does not support RBEv2.0, falling back to newer "
                    "API version (best effort).");
        best_effort = true;
    }

    return std::unique_ptr<BazelAction>{new (std::nothrow)
                                            BazelAction{network_,
                                                        root_digest,
                                                        command,
                                                        cwd,
                                                        output_files,
                                                        output_dirs,
                                                        env_vars,
                                                        properties,
                                                        best_effort}};
}

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
                auto reader =
                    TreeReader<BazelNetworkReader>{network_->CreateReader()};
                auto const result = reader.RecursivelyReadTreeLeafs(
                    info.digest, output_paths[i]);
                if (not result or
                    not RetrieveToPaths(
                        result->infos, result->paths, alternative)) {
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
    auto const blobs = network_->CreateReader().ReadOrdered(file_digests);
    if (blobs.size() != file_digests.size()) {
        Logger::Log(LogLevel::Warning,
                    "could not retrieve all requested blobs.");
        return false;
    }
    for (std::size_t i = 0; i < blobs.size(); ++i) {
        auto const gpos = artifact_pos[i];
        auto const type = artifacts_info[gpos].type;
        auto const& dst = output_paths[gpos];

        bool written = false;
        if (auto const path = blobs[i].GetFilePath()) {
            if (FileSystemManager::CreateDirectory(dst.parent_path()) and
                FileSystemManager::RemoveFile(dst)) {
                written = IsSymlinkObject(type)
                              ? FileSystemManager::CopySymlinkAs<
                                    /*kSetEpochTime=*/true>(*path, dst)
                              : FileSystemManager::CreateFileHardlinkAs<
                                    /*kSetEpochTime=*/true>(*path, dst, type);
            }
        }

        if (not written) {
            Logger::Log(LogLevel::Warning,
                        "staging to output path {} failed.",
                        dst.string());
            return false;
        }
    }
    return true;
}

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
    std::unordered_set<Artifact::ObjectInfo> missing;
    missing.reserve(artifacts_info.size());
    {
        auto back_map = BackMap<ArtifactDigest, Artifact::ObjectInfo>::Make(
            &artifacts_info,
            [](Artifact::ObjectInfo const& info) { return info.digest; });
        if (back_map == nullptr) {
            Logger::Log(LogLevel::Error, "BazelApi: Failed to create BackMap");
            return false;
        }
        auto missing_digests = api.GetMissingDigests(back_map->GetKeys());
        missing = back_map->GetValues(missing_digests);
    }

    // Recursively process trees.
    auto const reader =
        TreeReader<BazelNetworkReader>{network_->CreateReader()};
    for (auto const& info : missing) {
        if (not IsTreeObject(info.type)) {
            continue;
        }
        auto const result =
            reader.ReadDirectTreeEntries(info.digest, std::filesystem::path{});
        if (not result or not RetrieveToCas(result->infos, api)) {
            return false;
        }
    }
    return ::RetrieveToCas(missing, api, network_);
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
    std::unordered_set<gsl::not_null<Artifact::ObjectInfo const*>> missing;
    missing.reserve(artifacts_info.size());
    {
        auto back_map = BackMap<ArtifactDigest, Artifact::ObjectInfo>::Make(
            &artifacts_info,
            [](Artifact::ObjectInfo const& info) { return info.digest; });
        if (back_map == nullptr) {
            Logger::Log(LogLevel::Error, "BazelApi: Failed to create BackMap");
            return false;
        }
        auto missing_digests = api.GetMissingDigests(back_map->GetKeys());
        missing = back_map->GetReferences(missing_digests);
    }

    // Recursively process trees.
    std::atomic_bool failure{false};
    std::vector<Artifact::ObjectInfo> prerequisites{};
    std::mutex prerequisites_lock{};
    try {
        auto ts = TaskSystem{jobs};
        for (auto const& info : missing) {
            if (not IsTreeObject(info->type)) {
                continue;
            }
            ts.QueueTask(
                [this, info, &failure, &prerequisites, &prerequisites_lock]() {
                    auto reader = TreeReader<BazelNetworkReader>{
                        network_->CreateReader()};
                    auto const result = reader.ReadDirectTreeEntries(
                        info->digest, std::filesystem::path{});
                    if (not result) {
                        failure = true;
                        return;
                    }
                    std::unique_lock lock{prerequisites_lock};
                    prerequisites.insert(prerequisites.end(),
                                         result->infos.begin(),
                                         result->infos.end());
                });
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
        for (auto const& info : missing) {
            ts.QueueTask([this, info, &api, &failure, use_blob_splitting]() {
                if (use_blob_splitting and network_->BlobSplitSupport() and
                            api.BlobSpliceSupport()
                        ? ::RetrieveToCasSplitted(*info, *this, api, network_)
                        : ::RetrieveToCas({*info}, api, network_)) {
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

    if (failure) {
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

    return true;
}

[[nodiscard]] auto BazelApi::RetrieveToMemory(
    Artifact::ObjectInfo const& artifact_info) const noexcept
    -> std::optional<std::string> {
    auto reader = network_->CreateReader();
    if (auto blob = reader.ReadSingleBlob(artifact_info.digest)) {
        if (auto const content = blob->ReadContent()) {
            return *content;
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto BazelApi::Upload(std::unordered_set<ArtifactBlob>&& blobs,
                                    bool skip_find_missing) const noexcept
    -> bool {
    return network_->UploadBlobs(std::move(blobs), skip_find_missing);
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
            for (auto const& blob : reader.ReadOrdered(digests)) {
                if (auto const content = blob.ReadContent()) {
                    targets->emplace_back(*content);
                }
            }
        });
}

[[nodiscard]] auto BazelApi::IsAvailable(
    ArtifactDigest const& digest) const noexcept -> bool {
    return network_->IsAvailable(digest);
}

[[nodiscard]] auto BazelApi::GetMissingDigests(
    std::unordered_set<ArtifactDigest> const& digests) const noexcept
    -> std::unordered_set<ArtifactDigest> {
    return network_->FindMissingBlobs(digests);
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

[[nodiscard]] auto BazelApi::GetHashType() const noexcept
    -> HashFunction::Type {
    return network_->GetHashFunction().GetType();
}

[[nodiscard]] auto BazelApi::GetTempSpace() const noexcept -> TmpDir::Ptr {
    return network_->GetTempSpace();
}
