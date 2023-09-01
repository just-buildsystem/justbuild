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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_TLC_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_TLC_CLIENT_HPP

#include <memory>
#include <string>

#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/logging/logger.hpp"

/// Implements client side for service defined in:
/// src/buildtool/serve_api/serve_service/just_serve.proto
class ServeTargetLevelCacheClient {
  public:
    ServeTargetLevelCacheClient(std::string const& server, Port port) noexcept;

    /// \brief Retrieve the Git tree of a given commit, if known by the remote.
    /// \param[in] commit_id Hash of the Git commit to look up.
    /// \param[in] subdir Relative path of the tree inside commit.
    /// \returns The hash of the tree if commit found, nullopt otherwise.
    [[nodiscard]] auto ServeCommitTree(std::string const& commit_id,
                                       std::string const& subdir)
        -> std::optional<std::string>;

  private:
    std::unique_ptr<justbuild::just_serve::TargetLevelCache::Stub> stub_;
    Logger logger_{"RemoteTLCClient"};
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_TLC_CLIENT_HPP
