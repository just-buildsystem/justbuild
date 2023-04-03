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

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief API for local execution.
class GitApi final : public IExecutionApi {
  public:
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
                auto tree = RepositoryConfig::Instance().ReadTreeFromGitCAS(
                    info.digest.hash());
                if (not tree) {
                    return false;
                }
                for (auto const& [path, entry] : *tree) {
                    if (not RetrieveToPaths(
                            {Artifact::ObjectInfo{
                                ArtifactDigest{
                                    entry->Hash(), /*size*/ 0, entry->IsTree()},
                                entry->Type(),
                                false}},
                            {output_paths[i] / path})) {
                        return false;
                    }
                }
            }
            else {
                auto blob = RepositoryConfig::Instance().ReadBlobFromGitCAS(
                    info.digest.hash());
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
                auto tree = RepositoryConfig::Instance().ReadTreeFromGitCAS(
                    info.digest.hash());
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
                            ArtifactDigest{
                                entry->Hash(), /*size*/ 0, entry->IsTree()},
                            entry->Type(),
                            false}
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
                auto blob = RepositoryConfig::Instance().ReadBlobFromGitCAS(
                    info.digest.hash());
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

    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& /*artifacts_info*/,
        gsl::not_null<IExecutionApi*> const& /*api*/) noexcept
        -> bool override {
        // So far, no user of the git API ever calls RetrieveToCas.
        // Hence, we will implement it, only once the first use case arises
        // in order to avoid dead code.
        Logger::Log(LogLevel::Warning,
                    "Git API, RetrieveToCas not implemented yet.");
        return false;
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
        return RepositoryConfig::Instance()
            .ReadBlobFromGitCAS(digest.hash())
            .has_value();
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
};

#endif
