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
#include <variant>
#include <vector>

#include "gsl/gsl"
#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/execution_api/common/create_execution_api.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"

/// \brief Result union for the ServeTarget request.
/// Index 0 will contain the hash of the blob containing the logged
/// analysis/build failure received from the endpoint, as a string; this should
/// also trigger a local build fail.
/// Index 1 will contain the message of any INTERNAL error on the endpoint, as
/// a string; this should trigger a local build fail.
/// Index 2 will contain any other failure message, as a string; local builds
/// might be able to continue, but with a warning.
/// Index 3 will contain the target cache value information on success.
using serve_target_result_t =
    std::variant<std::string,
                 std::string,
                 std::string,
                 std::pair<TargetCacheEntry, Artifact::ObjectInfo>>;

/// Implements client side for Target service defined in:
/// src/buildtool/serve_api/serve_service/just_serve.proto
class TargetClient {
  public:
    explicit TargetClient(ServerAddress const& address) noexcept;

    /// \brief Retrieve the pair of TargetCacheEntry and ObjectInfo associated
    /// to the given key.
    /// \param[in] key The TargetCacheKey of an export target.
    /// \param[in] repo_key The RepositoryKey to upload as precondition.
    /// \returns A correspondingly populated result union, or nullopt if remote
    /// reported that the target was not found.
    [[nodiscard]] auto ServeTarget(const TargetCacheKey& key,
                                   const std::string& repo_key) const noexcept
        -> std::optional<serve_target_result_t>;

    /// \brief Retrieve the flexible config variables of an export target.
    /// \param[in] target_root_id Hash of target-level root tree.
    /// \param[in] target_file Relative path of the target file.
    /// \param[in] target Name of the target to interrogate.
    /// \returns The list of flexible config variables, or nullopt on errors.
    [[nodiscard]] auto ServeTargetVariables(std::string const& target_root_id,
                                            std::string const& target_file,
                                            std::string const& target)
        const noexcept -> std::optional<std::vector<std::string>>;

    /// \brief Retrieve the artifact digest of the blob containing the export
    /// target description fields required by just describe.
    /// \param[in] target_root_id Hash of target-level root tree.
    /// \param[in] target_file Relative path of the target file.
    /// \param[in] target Name of the target to interrogate.
    /// \returns The artifact digest, or nullopt on errors.
    [[nodiscard]] auto ServeTargetDescription(std::string const& target_root_id,
                                              std::string const& target_file,
                                              std::string const& target)
        const noexcept -> std::optional<ArtifactDigest>;

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
