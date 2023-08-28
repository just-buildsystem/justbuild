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

#include "src/buildtool/serve_api/serve_service/target_level_cache_server.hpp"

#include "fmt/core.h"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/storage/config.hpp"

auto TargetLevelCacheService::GetTreeFromCommit(
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
                            "ServeCommitTree: While retrieving tree of commit "
                            "{} from repository {}:\n{}",
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

auto TargetLevelCacheService::ServeCommitTree(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::ServeCommitTreeRequest* request,
    ::justbuild::just_serve::ServeCommitTreeResponse* response)
    -> ::grpc::Status {
    auto const& commit{request->commit()};
    auto const& subdir{request->subdir()};
    // try in local build root Git cache
    if (auto tree_id = GetTreeFromCommit(
            StorageConfig::GitRoot(), commit, subdir, logger_)) {
        *(response->mutable_tree()) = std::move(*tree_id);
        response->mutable_status()->CopyFrom(google::rpc::Status{});
        return ::grpc::Status::OK;
    }
    // try given extra repositories, in order
    for (auto const& path : RemoteServeConfig::KnownRepositories()) {
        if (auto tree_id = GetTreeFromCommit(path, commit, subdir, logger_)) {
            *(response->mutable_tree()) = std::move(*tree_id);
            response->mutable_status()->CopyFrom(google::rpc::Status{});
            return ::grpc::Status::OK;
        }
    }
    // commit not found
    google::rpc::Status status;
    status.set_code(grpc::StatusCode::NOT_FOUND);
    response->mutable_status()->CopyFrom(status);
    return ::grpc::Status::OK;
}
