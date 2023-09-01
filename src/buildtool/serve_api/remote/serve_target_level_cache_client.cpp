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

#include "src/buildtool/serve_api/remote/serve_target_level_cache_client.hpp"

#include "src/buildtool/common/remote/client_common.hpp"

ServeTargetLevelCacheClient::ServeTargetLevelCacheClient(
    std::string const& server,
    Port port) noexcept {
    stub_ = justbuild::just_serve::TargetLevelCache::NewStub(
        CreateChannelWithCredentials(server, port));
}

auto ServeTargetLevelCacheClient::ServeCommitTree(std::string const& commit_id,
                                                  std::string const& subdir)
    -> std::optional<std::string> {
    justbuild::just_serve::ServeCommitTreeRequest request{};
    request.set_commit(commit_id);
    request.set_subdir(subdir);

    grpc::ClientContext context;
    justbuild::just_serve::ServeCommitTreeResponse response;
    grpc::Status status = stub_->ServeCommitTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return std::nullopt;
    }
    if (response.status().code() != grpc::StatusCode::OK) {
        LogStatus(&logger_, LogLevel::Debug, response.status());
        return std::nullopt;
    }
    return response.tree();
}
