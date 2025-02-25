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
#include <iterator>
#include <memory>
#include <unordered_set>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_tree.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/back_map.hpp"
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
    : repo_config_{*repo_config} {}

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
            auto tree = repo_config_.ReadTreeFromGitCAS(info.digest.hash());
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
            auto blob = repo_config_.ReadBlobFromGitCAS(info.digest.hash());
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
            auto tree = repo_config_.ReadTreeFromGitCAS(info.digest.hash());
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
            auto blob = repo_config_.ReadBlobFromGitCAS(info.digest.hash());
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
    // Determine missing artifacts in api.
    std::unordered_set<gsl::not_null<Artifact::ObjectInfo const*>> missing;
    missing.reserve(artifacts_info.size());
    {
        auto back_map = BackMap<ArtifactDigest, Artifact::ObjectInfo>::Make(
            &artifacts_info,
            [](Artifact::ObjectInfo const& info) { return info.digest; });
        if (back_map == nullptr) {
            Logger::Log(LogLevel::Error, "GitApi: Failed to create BackMap");
            return false;
        }

        auto missing_digests = api.GetMissingDigests(back_map->GetKeys());
        missing = back_map->GetReferences(missing_digests);
    }

    // GitApi works in the native mode only.
    HashFunction const hash_function{HashFunction::Type::GitSHA1};

    // Collect blobs of missing artifacts from local CAS. Trees are
    // processed recursively before any blob is uploaded.
    std::unordered_set<ArtifactBlob> container;
    for (auto const& info : missing) {
        std::optional<std::string> content;
        // Recursively process trees.
        if (IsTreeObject(info->type)) {
            auto tree = repo_config_.ReadTreeFromGitCAS(info->digest.hash());
            if (not tree) {
                return false;
            }

            std::vector<Artifact::ObjectInfo> subentries;
            subentries.reserve(std::distance(tree->begin(), tree->end()));
            for (auto const& [path, entry] : *tree) {
                auto digest = ToArtifactDigest(*entry);
                if (not digest.has_value()) {
                    return false;
                }
                subentries.push_back(
                    Artifact::ObjectInfo{.digest = *std::move(digest),
                                         .type = entry->Type(),
                                         .failed = false});
            }
            if (not RetrieveToCas(subentries, api)) {
                return false;
            }
            content = tree->RawData();
        }
        else {
            content = repo_config_.ReadBlobFromGitCAS(info->digest.hash());
        }
        if (not content) {
            return false;
        }

        auto blob = ArtifactBlob::FromMemory(
            hash_function, info->type, *std::move(content));
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

auto GitApi::RetrieveToMemory(Artifact::ObjectInfo const& artifact_info)
    const noexcept -> std::optional<std::string> {
    return repo_config_.ReadBlobFromGitCAS(artifact_info.digest.hash());
}

auto GitApi::IsAvailable(ArtifactDigest const& digest) const noexcept -> bool {
    return repo_config_.ReadBlobFromGitCAS(digest.hash(), LogLevel::Trace)
        .has_value();
}
