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

#include <cstdio>

#include "gsl/gsl"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief API for local execution.
class GitApi final : public IExecutionApi {
  public:
    GitApi() : repo_config_{&RepositoryConfig::Instance()} {}
    explicit GitApi(gsl::not_null<RepositoryConfig*> const& repo_config)
        : repo_config_{repo_config} {}
    auto CreateAction(
        ArtifactDigest const& /*root_digest*/,
        std::vector<std::string> const& /*command*/,
        std::vector<std::string> const& /*output_files*/,
        std::vector<std::string> const& /*output_dirs*/,
        std::map<std::string, std::string> const& /*env_vars*/,
        std::map<std::string, std::string> const& /*properties*/) noexcept
        -> IExecutionAction::Ptr final {
        // Execution not supported from git cas
        return nullptr;
    }

    // NOLINTNEXTLINE(misc-no-recursion,google-default-arguments)
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths,
        IExecutionApi* /*alternative*/ = nullptr) noexcept -> bool override {
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
                    if (not RetrieveToPaths(
                            {Artifact::ObjectInfo{
                                .digest = ArtifactDigest{entry->Hash(),
                                                         /*size*/ 0,
                                                         entry->IsTree()},
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
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds,
        bool raw_tree) noexcept -> bool override {
        if (artifacts_info.size() != fds.size()) {
            Logger::Log(LogLevel::Error,
                        "different number of digests and file descriptors.");
            return false;
        }
        for (std::size_t i{}; i < artifacts_info.size(); ++i) {
            auto fd = fds[i];
            auto const& info = artifacts_info[i];
            if (IsTreeObject(info.type) and not raw_tree) {
                auto tree =
                    repo_config_->ReadTreeFromGitCAS(info.digest.hash());
                if (not tree) {
                    Logger::Log(LogLevel::Debug,
                                "Tree {} not known to git",
                                info.digest.hash());
                    return false;
                }
                auto json = nlohmann::json::object();
                for (auto const& [path, entry] : *tree) {
                    json[path] =
                        Artifact::ObjectInfo{
                            .digest = ArtifactDigest{entry->Hash(),
                                                     /*size*/ 0,
                                                     entry->IsTree()},
                            .type = entry->Type(),
                            .failed = false}
                            .ToString(/*size_unknown*/ true);
                }
                auto msg = json.dump(2) + "\n";
                if (gsl::owner<FILE*> out = fdopen(fd, "wb")) {  // NOLINT
                    std::fwrite(msg.data(), 1, msg.size(), out);
                    std::fclose(out);
                }
                else {
                    Logger::Log(LogLevel::Error,
                                "dumping to file descriptor {} failed.",
                                fd);
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
                auto msg = *blob;
                if (gsl::owner<FILE*> out = fdopen(fd, "wb")) {  // NOLINT
                    std::fwrite(msg.data(), 1, msg.size(), out);
                    std::fclose(out);
                }
                else {
                    Logger::Log(LogLevel::Error,
                                "dumping to file descriptor {} failed.",
                                fd);
                    return false;
                }
            }
        }
        return true;
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        gsl::not_null<IExecutionApi*> const& api) noexcept -> bool override {
        // Return immediately if target CAS is this CAS
        if (this == api) {
            return true;
        }

        // Determine missing artifacts in other CAS.
        std::vector<ArtifactDigest> digests;
        digests.reserve(artifacts_info.size());
        std::unordered_map<ArtifactDigest, Artifact::ObjectInfo> info_map;
        for (auto const& info : artifacts_info) {
            digests.emplace_back(info.digest);
            info_map[info.digest] = info;
        }
        auto const& missing_digests = api->IsAvailable(digests);
        std::vector<Artifact::ObjectInfo> missing_artifacts_info;
        missing_artifacts_info.reserve(missing_digests.size());
        for (auto const& digest : missing_digests) {
            missing_artifacts_info.emplace_back(info_map[digest]);
        }

        // Collect blobs of missing artifacts from local CAS. Trees are
        // processed recursively before any blob is uploaded.
        BlobContainer container{};
        for (auto const& info : missing_artifacts_info) {
            std::optional<std::string> content;
            // Recursively process trees.
            if (IsTreeObject(info.type)) {
                auto tree =
                    repo_config_->ReadTreeFromGitCAS(info.digest.hash());
                if (not tree) {
                    return false;
                }
                BlobContainer tree_deps_only_blobs{};
                for (auto const& [path, entry] : *tree) {
                    if (entry->IsTree()) {
                        if (not RetrieveToCas(
                                {Artifact::ObjectInfo{
                                    .digest = ArtifactDigest{entry->Hash(),
                                                             /*size*/ 0,
                                                             entry->IsTree()},
                                    .type = entry->Type(),
                                    .failed = false}},
                                api)) {
                            return false;
                        }
                    }
                    else {
                        content = entry->RawData();
                        if (not content) {
                            return false;
                        }
                        auto digest =
                            ArtifactDigest::Create<ObjectType::File>(*content);
                        try {
                            tree_deps_only_blobs.Emplace(
                                BazelBlob{digest,
                                          *content,
                                          IsExecutableObject(entry->Type())});
                        } catch (std::exception const& ex) {
                            Logger::Log(LogLevel::Error,
                                        "failed to emplace blob: ",
                                        ex.what());
                            return false;
                        }
                    }
                }
                if (not api->Upload(tree_deps_only_blobs)) {
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

            ArtifactDigest digest;
            if (IsTreeObject(info.type)) {
                digest = ArtifactDigest::Create<ObjectType::Tree>(*content);
            }
            else {
                digest = ArtifactDigest::Create<ObjectType::File>(*content);
            }

            try {
                container.Emplace(
                    BazelBlob{digest, *content, IsExecutableObject(info.type)});
            } catch (std::exception const& ex) {
                Logger::Log(
                    LogLevel::Error, "failed to emplace blob: ", ex.what());
                return false;
            }
        }

        // Upload blobs to remote CAS.
        return api->Upload(container, /*skip_find_missing=*/true);
    }

    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& artifact_info)
        -> std::optional<std::string> override {
        return repo_config_->ReadBlobFromGitCAS(artifact_info.digest.hash());
    }

    /// NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] auto Upload(BlobContainer const& /*blobs*/,
                              bool /*skip_find_missing*/ = false) noexcept
        -> bool override {
        // Upload to git cas not supported
        return false;
    }

    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const&
        /*artifacts*/) noexcept -> std::optional<ArtifactDigest> override {
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
    gsl::not_null<RepositoryConfig*> repo_config_;
};

#endif
