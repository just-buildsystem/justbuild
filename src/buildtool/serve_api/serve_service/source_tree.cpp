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

#include "src/buildtool/serve_api/serve_service/source_tree.hpp"

#include "fmt/core.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/storage/config.hpp"

auto SourceTreeService::GetSubtreeFromCommit(
    std::filesystem::path const& repo_path,
    std::string const& commit,
    std::string const& subdir,
    std::shared_ptr<Logger> const& logger) -> std::optional<std::string> {
    if (auto git_cas = GitCAS::Open(repo_path)) {
        if (auto repo = GitRepo::Open(git_cas)) {
            // wrap logger for GitRepo call
            auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                [logger, repo_path, commit](auto const& msg, bool fatal) {
                    if (fatal) {
                        auto err = fmt::format(
                            "ServeCommitTree: While retrieving subtree of "
                            "commit {} from repository {}:\n{}",
                            commit,
                            repo_path.string(),
                            msg);
                        logger->Emit(LogLevel::Trace, err);
                    }
                });
            if (auto tree_id = repo->GetSubtreeFromCommit(
                    commit, subdir, wrapped_logger)) {
                return tree_id;
            }
        }
    }
    return std::nullopt;
}

auto SourceTreeService::ServeCommitTree(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::ServeCommitTreeRequest* request,
    ::justbuild::just_serve::ServeCommitTreeResponse* response)
    -> ::grpc::Status {
    using ServeCommitTreeResponse =
        ::justbuild::just_serve::ServeCommitTreeResponse;
    auto const& commit{request->commit()};
    auto const& subdir{request->subdir()};
    // try in local build root Git cache
    if (auto tree_id = GetSubtreeFromCommit(
            StorageConfig::GitRoot(), commit, subdir, logger_)) {
        if (request->sync_tree()) {
            // sync tree with remote CAS
            auto digest = ArtifactDigest{*tree_id, 0, /*is_tree=*/true};
            auto repo = RepositoryConfig{};
            if (not repo.SetGitCAS(StorageConfig::GitRoot())) {
                auto str = fmt::format("Failed to SetGitCAS at {}",
                                       StorageConfig::GitRoot().string());
                logger_->Emit(LogLevel::Debug, str);
                response->set_status(ServeCommitTreeResponse::INTERNAL_ERROR);
                return ::grpc::Status::OK;
            }
            auto git_api = GitApi{&repo};
            if (not git_api.RetrieveToCas(
                    {Artifact::ObjectInfo{.digest = digest,
                                          .type = ObjectType::Tree}},
                    &(*remote_api_))) {
                auto str = fmt::format("Failed to sync tree {}", *tree_id);
                logger_->Emit(LogLevel::Debug, str);
                *(response->mutable_tree()) = std::move(*tree_id);
                response->set_status(ServeCommitTreeResponse::SYNC_ERROR);
                return ::grpc::Status::OK;
            }
        }
        // set response
        *(response->mutable_tree()) = std::move(*tree_id);
        response->set_status(ServeCommitTreeResponse::OK);
        return ::grpc::Status::OK;
    }
    // try given extra repositories, in order
    for (auto const& path : RemoteServeConfig::KnownRepositories()) {
        if (auto tree_id =
                GetSubtreeFromCommit(path, commit, subdir, logger_)) {
            if (request->sync_tree()) {
                // sync tree with remote CAS
                auto digest = ArtifactDigest{*tree_id, 0, /*is_tree=*/true};
                auto repo = RepositoryConfig{};
                if (not repo.SetGitCAS(path)) {
                    auto str =
                        fmt::format("Failed to SetGitCAS at {}", path.string());
                    logger_->Emit(LogLevel::Debug, str);
                    response->set_status(
                        ServeCommitTreeResponse::INTERNAL_ERROR);
                    return ::grpc::Status::OK;
                }
                auto git_api = GitApi{&repo};
                if (not git_api.RetrieveToCas(
                        {Artifact::ObjectInfo{.digest = digest,
                                              .type = ObjectType::Tree}},
                        &(*remote_api_))) {
                    auto str = fmt::format("Failed to sync tree {}", *tree_id);
                    logger_->Emit(LogLevel::Debug, str);
                    *(response->mutable_tree()) = std::move(*tree_id);
                    response->set_status(ServeCommitTreeResponse::SYNC_ERROR);
                }
            }
            // set response
            *(response->mutable_tree()) = std::move(*tree_id);
            response->set_status(ServeCommitTreeResponse::OK);
            return ::grpc::Status::OK;
        }
    }
    // commit not found
    response->set_status(ServeCommitTreeResponse::NOT_FOUND);
    return ::grpc::Status::OK;
}
