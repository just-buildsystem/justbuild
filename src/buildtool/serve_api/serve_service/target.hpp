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

#ifndef INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_HPP
#define INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_HPP

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"

// The target-level cache service.
class TargetService final : public justbuild::just_serve::Target::Service {
  public:
    explicit TargetService(
        gsl::not_null<RemoteServeConfig const*> const& serve_config,
        gsl::not_null<StorageConfig const*> const& storage_config,
        gsl::not_null<Storage const*> const& storage,
        gsl::not_null<ApiBundle const*> const& apis,
        ServeApi const* serve = nullptr) noexcept
        : serve_config_{*serve_config},
          storage_config_{*storage_config},
          storage_{*storage},
          apis_{*apis},
          serve_{serve} {}

    // Given a target-level caching key, returns the computed value. In doing
    // so, it can build on the associated endpoint passing the
    // RemoteExecutionProperties contained in the ServeTargetRequest.
    // The execution backend description, the resulting target cache value,
    // and all other artifacts referenced therein MUST be made available in
    // the CAS of the associated remote-execution endpoint.
    //
    // A failure to analyse or build a known target (i.e., a target for which
    // we have all the needed information available) should NOT be reported as
    // an error. Instead, the failure log should be uploaded as a blob to the
    // CAS of the associated remote-execution endpoint and its digest provided
    // to the client in the response field `log`. In this case, the field
    // `target_value` MUST not be set.
    //
    // If the status has a code different from `OK` or `NOT_FOUND`, the
    // response MUST not be used.
    //
    // Errors:
    // * `NOT_FOUND`: Unknown target or missing needed local information.
    //   This should only be used for non-fatal failures.
    // * `FAILED_PRECONDITION`: Required entries missing in the remote
    //   execution endpoint.
    // * `UNAVAILABLE`: Could not communicate with the remote-execution
    //   endpoint.
    // * `INVALID_ARGUMENT`: The client provided invalid arguments in request.
    // * `INTERNAL`: Internally, something is very broken.
    auto ServeTarget(
        ::grpc::ServerContext* /*context*/,
        const ::justbuild::just_serve::ServeTargetRequest* /*request*/,
        ::justbuild::just_serve::ServeTargetResponse* /*response*/)
        -> ::grpc::Status override;

    // Given the target-level root tree and the name of an export target,
    // returns the list of flexible variables from that target's description.
    //
    // If the status has a code different from `OK`, the response MUST not be
    // used.
    //
    // Errors:
    // * `FAILED_PRECONDITION`: An error occurred in retrieving the
    //   configuration of the requested target, such as missing entries
    //   (target-root, target file, target name), unparsable target file, or
    //   requested target not being of "type" : "export".
    // * `INTERNAL`: Internally, something is very broken.
    auto ServeTargetVariables(
        ::grpc::ServerContext* /*context*/,
        const ::justbuild::just_serve::ServeTargetVariablesRequest* request,
        ::justbuild::just_serve::ServeTargetVariablesResponse* response)
        -> ::grpc::Status override;

    // Given the target-level root tree and the name of an export target,
    // returns the digest of the blob containing the flexible variables field,
    // as well as the documentation fields pertaining tho the target and
    // its configuration variables, as taken from the target's description.
    // This information should be enough for a client to produce locally a
    // full description of said target.
    //
    // The server MUST make the returned blob available in the remote CAS.
    //
    // If the status has a code different from `OK`, the response MUST not be
    // used.
    //
    // Errors:
    // * `FAILED_PRECONDITION`: An error occurred in retrieving the
    //   configuration of the requested target, such as missing entries
    //   (target-root, target file, target name), unparsable target file, or
    //   requested target not being of "type" : "export".
    // * `UNAVAILABLE`: Could not communicate with the remote CAS.
    // * `INTERNAL`: Internally, something is very broken.
    auto ServeTargetDescription(
        ::grpc::ServerContext* /*context*/,
        const ::justbuild::just_serve::ServeTargetDescriptionRequest* request,
        ::justbuild::just_serve::ServeTargetDescriptionResponse* response)
        -> ::grpc::Status override;

  private:
    RemoteServeConfig const& serve_config_;
    StorageConfig const& storage_config_;
    Storage const& storage_;
    ApiBundle const& apis_;
    ServeApi const* const serve_ = nullptr;
    std::shared_ptr<Logger> logger_{std::make_shared<Logger>("target-service")};

    // type of dispatch list; reduces verbosity
    using dispatch_t = std::vector<
        std::pair<std::map<std::string, std::string>, ServerAddress>>;

    /// \brief Get from remote and parse the endpoint configuration. The method
    /// also ensures the content has the expected format.
    /// \returns The dispatch list stored as JSON object on success or an
    /// unexpected error as grpc::Status.
    [[nodiscard]] auto GetDispatchList(
        ArtifactDigest const& dispatch_digest) noexcept
        -> expected<dispatch_t, ::grpc::Status>;

    /// \brief Handles the processing of the log after a failed analysis or
    /// build. Will populate the response as needed and set the status to be
    /// returned to the client.
    /// \param failure_scope String stating where the failure occurred, to be
    /// included in the local error messaging.
    [[nodiscard]] auto HandleFailureLog(
        std::filesystem::path const& logfile,
        std::string const& failure_scope,
        ::justbuild::just_serve::ServeTargetResponse* response) noexcept
        -> ::grpc::Status;
};

#endif  // INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_HPP
