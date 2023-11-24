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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_TARGET_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_TARGET_CLIENT_HPP

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/execution_api/common/create_execution_api.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"

/// Implements client side for Target service defined in:
/// src/buildtool/serve_api/serve_service/just_serve.proto
class TargetClient {
  public:
    TargetClient(std::string const& server, Port port) noexcept;

    /// \brief Retrieve the pair of TargetCacheEntry and ObjectInfo associated
    /// to the given key.
    /// \param[in] key The TargetCacheKey of an export target
    /// \returns Pair of cache entry and its object info on success or nullopt.
    [[nodiscard]] auto ServeTarget(const TargetCacheKey& key)
        -> std::optional<std::pair<TargetCacheEntry, Artifact::ObjectInfo>>;

    /// \brief Retrieve the flexible config variables of an export target.
    /// \param[in] target_root_id Hash of target-level root tree.
    /// \param[in] target_file Relative path of the target file.
    /// \param[in] target Name of the target to interrogate.
    /// \returns The list of flexible config variables, or nullopt on errors.
    [[nodiscard]] auto ServeTargetVariables(std::string const& target_root_id,
                                            std::string const& target_file,
                                            std::string const& target)
        -> std::optional<std::vector<std::string>>;

  private:
    std::unique_ptr<justbuild::just_serve::Target::Stub> stub_;
    Logger logger_{"RemoteTargetClient"};
    gsl::not_null<IExecutionApi::Ptr> const remote_api_{
        CreateExecutionApi(RemoteExecutionConfig::RemoteAddress(),
                           std::nullopt,
                           "remote-execution")};
    gsl::not_null<IExecutionApi::Ptr> const local_api_{
        CreateExecutionApi(std::nullopt)};
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_TARGET_CLIENT_HPP
