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

#include "src/buildtool/execution_api/git/git_api.hpp"

#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/artifact_blob.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_tree.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"

namespace {
[[nodiscard]] auto ToArtifactDigest(GitTreeEntry const& entry) noexcept
    -> std::optional<ArtifactDigest> {
    auto digest = ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                                entry.Hash(),
                                                /*size=*/0,
                                                entry.IsTree());
    if (not digest) {
        return std::nullopt;
    }
    return *std::move(digest);
}
}  // namespace

GitApi::GitApi(gsl::not_null<RepositoryConfig const*> const& repo_config)
    : repo_config_{repo_config} {}

auto GitApi::RetrieveToPaths(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<std::filesystem::path> const& output_paths) const noexcept
    -> bool {
    if (artifacts_info.size() != output_paths.size()) {
        Logger::Log(LogLevel::Error,
                    "different number of digests and output paths.");
        return false;
    }
    for (std::size_t i{}; i < artifacts_info.size(); ++i) {
        auto const& info = artifacts_info[i];
        if (IsTreeObject(info.type)) {
            auto tree = repo_config_->ReadTreeFromGitCAS(info.digest.hash());
            if (not tree) {
                return false;
            }
            for (auto const& [path, entry] : *tree) {
                auto digest = ToArtifactDigest(*entry);
                if (not digest or
                    not RetrieveToPaths(
                        {Artifact::ObjectInfo{.digest = *std::move(digest),
                                              .type = entry->Type(),
                                              .failed = false}},
                        {output_paths[i] / path})) {
                    return false;
                }
            }
        }
        else {
            auto blob = repo_config_->ReadBlobFromGitCAS(info.digest.hash());
            if (not blob) {
                return false;
            }
            if (not FileSystemManager::CreateDirectory(
                    output_paths[i].parent_path()) or
                not FileSystemManager::WriteFileAs</*kSetEpochTime=*/true,
                                                   /*kSetWritable=*/true>(
                    *blob, output_paths[i], info.type)) {
                Logger::Log(LogLevel::Error,
                            "staging to output path {} failed.",
                            output_paths[i].string());
                return false;
            }
        }
    }
    return true;
}

auto GitApi::RetrieveToFds(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    std::vector<int> const& fds,
    bool raw_tree) const noexcept -> bool {
    if (artifacts_info.size() != fds.size()) {
        Logger::Log(LogLevel::Error,
                    "different number of digests and file descriptors.");
        return false;
    }
    for (std::size_t i{}; i < artifacts_info.size(); ++i) {
        auto const& info = artifacts_info[i];

        std::string content;
        if (IsTreeObject(info.type) and not raw_tree) {
            auto tree = repo_config_->ReadTreeFromGitCAS(info.digest.hash());
            if (not tree) {
                Logger::Log(LogLevel::Debug,
                            "Tree {} not known to git",
                            info.digest.hash());
                return false;
            }

            try {
                auto json = nlohmann::json::object();
                for (auto const& [path, entry] : *tree) {
                    auto digest = ToArtifactDigest(*entry);
                    if (not digest) {
                        return false;
                    }
                    json[path] =
                        Artifact::ObjectInfo{.digest = *std::move(digest),
                                             .type = entry->Type(),
                                             .failed = false}
                            .ToString(/*size_unknown*/ true);
                }
                content = json.dump(2) + "\n";
            } catch (...) {
                return false;
            }
        }
        else {
            auto blob = repo_config_->ReadBlobFromGitCAS(info.digest.hash());
            if (not blob) {
                Logger::Log(LogLevel::Debug,
                            "Blob {} not known to git",
                            info.digest.hash());
                return false;
            }
            content = *std::move(blob);
        }

        if (gsl::owner<FILE*> out = fdopen(fds[i], "wb")) {  // NOLINT
            std::fwrite(content.data(), 1, content.size(), out);
            std::fclose(out);
        }
        else {
            Logger::Log(LogLevel::Error,
                        "dumping to file descriptor {} failed.",
                        fds[i]);
            return false;
        }
    }
    return true;
}

auto GitApi::RetrieveToCas(
    std::vector<Artifact::ObjectInfo> const& artifacts_info,
    IExecutionApi const& api) const noexcept -> bool {
    // Determine missing artifacts in other CAS.
    auto missing_artifacts_info = GetMissingArtifactsInfo<Artifact::ObjectInfo>(
        api,
        artifacts_info.begin(),
        artifacts_info.end(),
        [](Artifact::ObjectInfo const& info) { return info.digest; });
    if (not missing_artifacts_info) {
        Logger::Log(LogLevel::Error,
                    "GitApi: Failed to retrieve the missing artifacts");
        return false;
    }

    // GitApi works in the native mode only.
    HashFunction const hash_function{HashFunction::Type::GitSHA1};

    // Collect blobs of missing artifacts from local CAS. Trees are
    // processed recursively before any blob is uploaded.
    std::unordered_set<ArtifactBlob> container;
    for (auto const& dgst : missing_artifacts_info->digests) {
        auto const& info = missing_artifacts_info->back_map[dgst];
        std::optional<std::string> content;
        // Recursively process trees.
        if (IsTreeObject(info.type)) {
            auto tree = repo_config_->ReadTreeFromGitCAS(info.digest.hash());
            if (not tree) {
                return false;
            }
            std::unordered_set<ArtifactBlob> tree_deps_only_blobs;
            for (auto const& [path, entry] : *tree) {
                if (entry->IsTree()) {
                    auto digest = ToArtifactDigest(*entry);
                    if (not digest or
                        not RetrieveToCas(
                            {Artifact::ObjectInfo{.digest = *std::move(digest),
                                                  .type = entry->Type(),
                                                  .failed = false}},
                            api)) {
                        return false;
                    }
                }
                else {
                    auto const& entry_content = entry->RawData();
                    if (not entry_content) {
                        return false;
                    }
                    auto digest =
                        ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                            hash_function, *entry_content);
                    // Collect blob and upload to remote CAS if transfer
                    // size reached.
                    if (not UpdateContainerAndUpload(
                            &tree_deps_only_blobs,
                            ArtifactBlob{std::move(digest),
                                         *entry_content,
                                         IsExecutableObject(entry->Type())},
                            /*exception_is_fatal=*/true,
                            [&api](std::unordered_set<ArtifactBlob>&& blobs)
                                -> bool {
                                return api.Upload(std::move(blobs));
                            })) {
                        return false;
                    }
                }
            }
            // Upload remaining blobs.
            if (not api.Upload(std::move(tree_deps_only_blobs))) {
                return false;
            }
            content = tree->RawData();
        }
        else {
            content = repo_config_->ReadBlobFromGitCAS(info.digest.hash());
        }
        if (not content) {
            return false;
        }

        ArtifactDigest digest =
            IsTreeObject(info.type)
                ? ArtifactDigestFactory::HashDataAs<ObjectType::Tree>(
                      hash_function, *content)
                : ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                      hash_function, *content);

        // Collect blob and upload to remote CAS if transfer size reached.
        if (not UpdateContainerAndUpload(
                &container,
                ArtifactBlob{std::move(digest),
                             std::move(*content),
                             IsExecutableObject(info.type)},
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

auto GitApi::RetrieveToMemory(Artifact::ObjectInfo const& artifact_info)
    const noexcept -> std::optional<std::string> {
    return repo_config_->ReadBlobFromGitCAS(artifact_info.digest.hash());
}

auto GitApi::IsAvailable(ArtifactDigest const& digest) const noexcept -> bool {
    return repo_config_->ReadBlobFromGitCAS(digest.hash(), LogLevel::Trace)
        .has_value();
}

auto GitApi::GetMissingDigests(
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
