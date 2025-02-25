// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/local/local_api.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <new>  // std::nothrow
#include <sstream>
#include <utility>  // std::move

#include <grpcpp/support/status.h>

#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/stream_dumper.hpp"
#include "src/buildtool/execution_api/common/tree_reader.hpp"
#include "src/buildtool/execution_api/execution_service/cas_utils.hpp"
#include "src/buildtool/execution_api/local/local_action.hpp"
#include "src/buildtool/execution_api/local/local_cas_reader.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/back_map.hpp"
#include "src/utils/cpp/expected.hpp"

namespace {
[[nodiscard]] auto CreateFallbackApi(
    Storage const& storage,
    RepositoryConfig const* repo_config) noexcept -> std::optional<GitApi> {
    if (repo_config == nullptr or
        not ProtocolTraits::IsNative(storage.GetHashFunction().GetType())) {
        return std::nullopt;
    }
    return GitApi{repo_config};
}
}  // namespace

LocalApi::LocalApi(gsl::not_null<LocalContext const*> const& local_context,
                   RepositoryConfig const* repo_config) noexcept
    : local_context_{*local_context},
      git_api_{CreateFallbackApi(*local_context->storage, repo_config)} {}

auto LocalApi::CreateAction(
    ArtifactDigest const& root_digest,
    std::vector<std::string> const& command,
    std::string const& cwd,
    std::vector<std::string> const& output_files,
    std::vector<std::string> const& output_dirs,
    std::map<std::string, std::string> const& env_vars,
    std::map<std::string, std::string> const& properties) const noexcept
    -> IExecutionAction::Ptr {
    return IExecutionAction::Ptr{new (std::nothrow) LocalAction{&local_context_,
                                                                root_digest,
                                                                command,
                                                                cwd,
                                                                output_files,
                                                                output_dirs,
                                                                env_vars,
                                                                properties}};
}

// NOLINTNEXTLINE(google-default-arguments)
auto LocalApi::RetrieveToPaths(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<std::filesystem::path> const& output_paths,
    IExecutionApi const* /*alternative*/) const noexcept -> bool {
    if (artifacts_info.size() != output_paths.size()) {
        Logger::Log(LogLevel::Error,
                    "different number of digests and output paths.");
        return false;
    }

    auto const reader =
        TreeReader<LocalCasReader>{&local_context_.storage->CAS()};
    for (std::size_t i = 0; i < artifacts_info.size(); ++i) {
        auto const& info = artifacts_info[i];
        if (not reader.StageTo({info}, {output_paths[i]})) {
            if (not git_api_ or
                not git_api_->RetrieveToPaths({info}, {output_paths[i]})) {
                Logger::Log(LogLevel::Error,
                            "staging to output path {} failed.",
                            output_paths[i].string());
                return false;
            }
        }
    }
    return true;
}

// NOLINTNEXTLINE(google-default-arguments)
auto LocalApi::RetrieveToFds(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<int> const& fds,
    bool raw_tree,
    IExecutionApi const* /*alternative*/) const noexcept -> bool {
    auto dumper = StreamDumper<LocalCasReader>{&local_context_.storage->CAS()};
    return CommonRetrieveToFds(
        artifacts_info,
        fds,
        [&dumper, &raw_tree](Artifact::ObjectInfo const& info,
                             gsl::not_null<FILE*> const& out) {
            return dumper.DumpToStream(info, out, raw_tree);
        },
        [&git_api = git_api_, &raw_tree](Artifact::ObjectInfo const& info,
                                         int fd) {
            return git_api and git_api->IsAvailable(info.digest) and
                   git_api->RetrieveToFds({info}, {fd}, raw_tree);
        });
}

auto LocalApi::RetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    IExecutionApi const& api) const noexcept -> bool {
    // Return immediately if target CAS is this CAS
    if (this == &api) {
        return true;
    }

    // Determine missing artifacts in other CAS.
    std::unordered_set<gsl::not_null<Artifact::ObjectInfo const*>> missing;
    missing.reserve(artifacts_info.size());
    {
        auto back_map = BackMap<ArtifactDigest, Artifact::ObjectInfo>::Make(
            &artifacts_info,
            [](Artifact::ObjectInfo const& info) { return info.digest; });
        if (back_map == nullptr) {
            Logger::Log(LogLevel::Error, "LocalApi: Failed to create BackMap");
            return false;
        }
        auto missing_digests = api.GetMissingDigests(back_map->GetKeys());
        missing = back_map->GetReferences(missing_digests);
    }

    // Collect blobs of missing artifacts from local CAS. Trees are
    // processed recursively before any blob is uploaded.
    std::unordered_set<ArtifactBlob> container;
    auto const& cas = local_context_.storage->CAS();
    for (auto const& info : missing) {
        // Recursively process trees.
        if (IsTreeObject(info->type)) {
            auto reader = TreeReader<LocalCasReader>{&cas};
            auto const& result = reader.ReadDirectTreeEntries(
                info->digest, std::filesystem::path{});
            if (not result or not RetrieveToCas(result->infos, api)) {
                return false;
            }
        }

        // Determine artifact path.
        auto path =
            IsTreeObject(info->type)
                ? cas.TreePath(info->digest)
                : cas.BlobPath(info->digest, IsExecutableObject(info->type));
        if (not path.has_value()) {
            return false;
        }

        auto blob =
            ArtifactBlob::FromFile(local_context_.storage_config->hash_function,
                                   info->type,
                                   *std::move(path));
        if (not blob.has_value()) {
            return false;
        }

        // Collect blob and upload to remote CAS if transfer size reached.
        if (not UpdateContainerAndUpload(
                &container,
                *std::move(blob),
                /*exception_is_fatal=*/true,
                [&api](std::unordered_set<ArtifactBlob>&& blobs) {
                    return api.Upload(std::move(blobs),
                                      /*skip_find_missing=*/true);
                })) {
            return false;
        }
    }

    // Upload remaining blobs to remote CAS.
    return api.Upload(std::move(container), /*skip_find_missing=*/true);
}

auto LocalApi::RetrieveToMemory(Artifact::ObjectInfo const& artifact_info)
    const noexcept -> std::optional<std::string> {
    std::optional<std::filesystem::path> location{};
    if (IsTreeObject(artifact_info.type)) {
        location = local_context_.storage->CAS().TreePath(artifact_info.digest);
    }
    else {
        location = local_context_.storage->CAS().BlobPath(
            artifact_info.digest, IsExecutableObject(artifact_info.type));
    }
    std::optional<std::string> content = std::nullopt;
    if (location) {
        content = FileSystemManager::ReadFile(*location);
    }
    if (not content and git_api_) {
        content = git_api_->RetrieveToMemory(artifact_info);
    }
    return content;
}

auto LocalApi::Upload(std::unordered_set<ArtifactBlob>&& blobs,
                      bool /*skip_find_missing*/) const noexcept -> bool {
    // Blobs could have been received over the network, so a simple failure
    // could result in lost traffic. Try add all blobs and fail if at least
    // one is corrupted.
    std::size_t const valid_count = std::count_if(
        blobs.begin(),
        blobs.end(),
        [&cas = local_context_.storage->CAS()](ArtifactBlob const& blob) {
            std::optional<ArtifactDigest> cas_digest;
            if (auto const content = blob.ReadContent()) {
                cas_digest = blob.GetDigest().IsTree()
                                 ? cas.StoreTree(*content)
                                 : cas.StoreBlob(*content, blob.IsExecutable());
            }
            return cas_digest and *cas_digest == blob.GetDigest();
        });
    return valid_count == blobs.size();
}

auto LocalApi::UploadTree(
    std::vector<DependencyGraph::NamedArtifactNodePtr> const& artifacts)
    const noexcept -> std::optional<ArtifactDigest> {
    auto build_root = DirectoryTree::FromNamedArtifacts(artifacts);
    if (not build_root) {
        Logger::Log(LogLevel::Debug,
                    "failed to create build root from artifacts.");
        return std::nullopt;
    }

    auto const& cas = local_context_.storage->CAS();
    if (ProtocolTraits::IsNative(cas.GetHashFunction().GetType())) {
        return CommonUploadTreeNative(*this, *build_root);
    }
    return CommonUploadTreeCompatible(
        *this,
        *build_root,
        [&cas](std::vector<ArtifactDigest> const& digests,
               gsl::not_null<std::vector<std::string>*> const& targets) {
            targets->reserve(digests.size());
            for (auto const& digest : digests) {
                auto p = cas.BlobPath(digest,
                                      /*is_executable=*/false);
                auto content = FileSystemManager::ReadFile(*p);
                targets->emplace_back(*content);
            }
        });
}

auto LocalApi::IsAvailable(ArtifactDigest const& digest) const noexcept
    -> bool {
    auto found = static_cast<bool>(
        digest.IsTree()
            ? local_context_.storage->CAS().TreePath(digest)
            : local_context_.storage->CAS().BlobPath(digest, false));
    if ((not found) and git_api_) {
        if (git_api_->IsAvailable(digest)) {
            auto plain_local = LocalApi(&local_context_);
            auto obj_info = std::vector<Artifact::ObjectInfo>{};
            obj_info.push_back(Artifact::ObjectInfo{
                digest,
                digest.IsTree() ? ObjectType::Tree : ObjectType::File,
                false});
            found = git_api_->RetrieveToCas(obj_info, plain_local);
        }
    }
    return found;
}

auto LocalApi::GetMissingDigests(
    std::unordered_set<ArtifactDigest> const& digests) const noexcept
    -> std::unordered_set<ArtifactDigest> {
    std::unordered_set<ArtifactDigest> result;
    result.reserve(digests.size());
    for (auto const& digest : digests) {
        if (not IsAvailable(digest)) {
            result.emplace(digest);
        }
    }
    return result;
}

auto LocalApi::SplitBlob(ArtifactDigest const& blob_digest) const noexcept
    -> std::optional<std::vector<ArtifactDigest>> {
    Logger::Log(LogLevel::Debug, "SplitBlob({})", blob_digest.hash());
    auto split_result =
        CASUtils::SplitBlobFastCDC(blob_digest, *local_context_.storage);
    if (not split_result) {
        Logger::Log(LogLevel::Error, split_result.error().error_message());
        return std::nullopt;
    }
    auto const& chunk_digests = *split_result;
    Logger::Log(LogLevel::Debug, [&blob_digest, &chunk_digests]() {
        std::stringstream ss{};
        ss << "Split blob " << blob_digest.hash() << ":" << blob_digest.size()
           << " into " << chunk_digests.size() << " chunks: [ ";
        for (auto const& chunk_digest : chunk_digests) {
            ss << chunk_digest.hash() << ":" << chunk_digest.size() << " ";
        }
        ss << "]";
        return ss.str();
    });
    return *std::move(split_result);
}

auto LocalApi::SpliceBlob(ArtifactDigest const& blob_digest,
                          std::vector<ArtifactDigest> const& chunk_digests)
    const noexcept -> std::optional<ArtifactDigest> {
    Logger::Log(LogLevel::Debug,
                "SpliceBlob({}, {} chunks)",
                blob_digest.hash(),
                chunk_digests.size());

    auto splice_result = CASUtils::SpliceBlob(
        blob_digest, chunk_digests, *local_context_.storage);
    if (not splice_result) {
        Logger::Log(LogLevel::Error, splice_result.error().error_message());
        return std::nullopt;
    }
    return *std::move(splice_result);
}

auto LocalApi::GetHashType() const noexcept -> HashFunction::Type {
    return local_context_.storage_config->hash_function.GetType();
}
