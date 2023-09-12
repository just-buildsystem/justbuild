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

#ifndef TARGET_LEVEL_CACHE_SERVER_HPP
#define TARGET_LEVEL_CACHE_SERVER_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gsl/gsl"
#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/logging/logger.hpp"

class TargetLevelCacheService final
    : public justbuild::just_serve::TargetLevelCache::Service {

  public:
    // Retrieve the tree of a commit.
    //
    // This request interrogates the service whether it knows a given Git
    // commit. If requested commit is found, it provides the commit's
    // Git-tree identifier.
    //
    // Errors:
    //
    // * `NOT_FOUND`: The requested commit could not be found.
    auto ServeCommitTree(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::ServeCommitTreeRequest* request,
        ::justbuild::just_serve::ServeCommitTreeResponse* response)
        -> ::grpc::Status override;

  private:
    std::shared_ptr<Logger> logger_{std::make_shared<Logger>("serve-service")};

    // remote execution endpoint
    gsl::not_null<IExecutionApi::Ptr> const remote_api_{
        CreateExecutionApi(RemoteExecutionConfig::RemoteAddress())};
    // local api
    gsl::not_null<IExecutionApi::Ptr> const local_api_{
        CreateExecutionApi(std::nullopt)};

    [[nodiscard]] static auto CreateExecutionApi(
        std::optional<RemoteExecutionConfig::ServerAddress> const& address)
        -> gsl::not_null<IExecutionApi::Ptr>;

    [[nodiscard]] static auto GetTreeFromCommit(
        std::filesystem::path const& repo_path,
        std::string const& commit,
        std::string const& subdir,
        std::shared_ptr<Logger> const& logger) -> std::optional<std::string>;
};

#endif  // TARGET_LEVEL_CACHE_SERVER_HPP
