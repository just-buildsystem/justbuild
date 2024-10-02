// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_GIT_GIT_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_GIT_GIT_API_HPP

#include <cstddef>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief API for local execution.
class GitApi final : public IExecutionApi {
  public:
    GitApi() = delete;
    explicit GitApi(gsl::not_null<const RepositoryConfig*> const& repo_config)
        : repo_config_{repo_config} {}
    [[nodiscard]] auto CreateAction(
        ArtifactDigest const& /*root_digest*/,
        std::vector<std::string> const& /*command*/,
        std::string const& /*cwd*/,
        std::vector<std::string> const& /*output_files*/,
        std::vector<std::string> const& /*output_dirs*/,
        std::map<std::string, std::string> const& /*env_vars*/,
        std::map<std::string, std::string> const& /*properties*/) const noexcept
        -> IExecutionAction::Ptr final {
        // Execution not supported from git cas
        return nullptr;
    }

    // NOLINTNEXTLINE(misc-no-recursion,google-default-arguments)
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths,
        IExecutionApi const* /*alternative*/ = nullptr) const noexcept
        -> bool override {
        if (artifacts_info.size() != output_paths.size()) {
            Logger::Log(LogLevel::Error,
                        "different number of digests and output paths.");
            return false;
        }
        for (std::size_t i{}; i < artifacts_info.size(); ++i) {
            auto const& info = artifacts_info[i];
            if (IsTreeObject(info.type)) {
                auto tree =
                    repo_config_->ReadTreeFromGitCAS(info.digest.hash());
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
                auto blob =
                    repo_config_->ReadBlobFromGitCAS(info.digest.hash());
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

    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds,
        bool raw_tree) const noexcept -> bool override {
        if (artifacts_info.size() != fds.size()) {
            Logger::Log(LogLevel::Error,
                        "different number of digests and file descriptors.");
            return false;
        }
        for (std::size_t i{}; i < artifacts_info.size(); ++i) {
            auto const& info = artifacts_info[i];

            std::string content;
            if (IsTreeObject(info.type) and not raw_tree) {
                auto tree =
                    repo_config_->ReadTreeFromGitCAS(info.digest.hash());
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
                auto blob =
                    repo_config_->ReadBlobFromGitCAS(info.digest.hash());
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

    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        IExecutionApi const& api) const noexcept -> bool override {
        // Return immediately if target CAS is this CAS
        if (this == &api) {
            return true;
        }

        // Determine missing artifacts in other CAS.
        auto missing_artifacts_info =
            GetMissingArtifactsInfo<Artifact::ObjectInfo>(
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
        ArtifactBlobContainer container{};
        for (auto const& dgst : missing_artifacts_info->digests) {
            auto const& info = missing_artifacts_info->back_map[dgst];
            std::optional<std::string> content;
            // Recursively process trees.
            if (IsTreeObject(info.type)) {
                auto tree =
                    repo_config_->ReadTreeFromGitCAS(info.digest.hash());
                if (not tree) {
                    return false;
                }
                ArtifactBlobContainer tree_deps_only_blobs{};
                for (auto const& [path, entry] : *tree) {
                    if (entry->IsTree()) {
                        auto digest = ToArtifactDigest(*entry);
                        if (not digest or
                            not RetrieveToCas({Artifact::ObjectInfo{
                                                  .digest = *std::move(digest),
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
                        if (not UpdateContainerAndUpload<ArtifactDigest>(
                                &tree_deps_only_blobs,
                                ArtifactBlob{std::move(digest),
                                             *entry_content,
                                             IsExecutableObject(entry->Type())},
                                /*exception_is_fatal=*/true,
                                [&api](ArtifactBlobContainer&& blobs) -> bool {
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
            if (not UpdateContainerAndUpload<ArtifactDigest>(
                    &container,
                    ArtifactBlob{std::move(digest),
                                 std::move(*content),
                                 IsExecutableObject(info.type)},
                    /*exception_is_fatal=*/true,
                    [&api](ArtifactBlobContainer&& blobs) {
                        return api.Upload(std::move(blobs),
                                          /*skip_find_missing=*/true);
                    })) {
                return false;
            }
        }

        // Upload remaining blobs to remote CAS.
        return api.Upload(std::move(container), /*skip_find_missing=*/true);
    }

    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& artifact_info) const noexcept
        -> std::optional<std::string> override {
        return repo_config_->ReadBlobFromGitCAS(artifact_info.digest.hash());
    }

    /// NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto Upload(ArtifactBlobContainer&& /*blobs*/,
                              bool /*skip_find_missing*/ = false) const noexcept
        -> bool override {
        // Upload to git cas not supported
        return false;
    }

    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const&
        /*artifacts*/) const noexcept
        -> std::optional<ArtifactDigest> override {
        // Upload to git cas not supported
        return std::nullopt;
    }

    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool override {
        return repo_config_->ReadBlobFromGitCAS(digest.hash()).has_value();
    }

    [[nodiscard]] auto IsAvailable(std::vector<ArtifactDigest> const& digests)
        const noexcept -> std::vector<ArtifactDigest> override {
        std::vector<ArtifactDigest> result;
        for (auto const& digest : digests) {
            if (not IsAvailable(digest)) {
                result.push_back(digest);
            }
        }
        return result;
    }

  private:
    gsl::not_null<const RepositoryConfig*> repo_config_;

    [[nodiscard]] static auto ToArtifactDigest(
        GitTreeEntry const& entry) noexcept -> std::optional<ArtifactDigest> {
        auto digest = ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                                    entry.Hash(),
                                                    /*size=*/0,
                                                    entry.IsTree());
        if (not digest) {
            return std::nullopt;
        }
        return *std::move(digest);
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_GIT_GIT_API_HPP
