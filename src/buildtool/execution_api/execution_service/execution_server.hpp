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

#ifndef EXECUTION_SERVER_HPP
#define EXECUTION_SERVER_HPP

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/local/local_storage.hpp"
#include "src/buildtool/logging/logger.hpp"

class ExecutionServiceImpl final : public bazel_re::Execution::Service {
  public:
    ExecutionServiceImpl() = default;
    // Execute an action remotely.
    //
    // In order to execute an action, the client must first upload all of the
    // inputs, the
    // [Command][build.bazel.remote.execution.v2.Command] to run, and the
    // [Action][build.bazel.remote.execution.v2.Action] into the
    // [ContentAddressableStorage][build.bazel.remote.execution.v2.ContentAddressableStorage].
    // It then calls `Execute` with an `action_digest` referring to them. The
    // server will run the action and eventually return the result.
    //
    // The input `Action`'s fields MUST meet the various canonicalization
    // requirements specified in the documentation for their types so that it
    // has the same digest as other logically equivalent `Action`s. The server
    // MAY enforce the requirements and return errors if a non-canonical input
    // is received. It MAY also proceed without verifying some or all of the
    // requirements, such as for performance reasons. If the server does not
    // verify the requirement, then it will treat the `Action` as distinct from
    // another logically equivalent action if they hash differently.
    //
    // Returns a stream of
    // [google.longrunning.Operation][google.longrunning.Operation] messages
    // describing the resulting execution, with eventual `response`
    // [ExecuteResponse][build.bazel.remote.execution.v2.ExecuteResponse]. The
    // `metadata` on the operation is of type
    // [ExecuteOperationMetadata][build.bazel.remote.execution.v2.ExecuteOperationMetadata].
    //
    // If the client remains connected after the first response is returned
    // after the server, then updates are streamed as if the client had called
    // [WaitExecution][build.bazel.remote.execution.v2.Execution.WaitExecution]
    // until the execution completes or the request reaches an error. The
    // operation can also be queried using [Operations
    // API][google.longrunning.Operations.GetOperation].
    //
    // The server NEED NOT implement other methods or functionality of the
    // Operations API.
    //
    // Errors discovered during creation of the `Operation` will be reported
    // as gRPC Status errors, while errors that occurred while running the
    // action will be reported in the `status` field of the `ExecuteResponse`.
    // The server MUST NOT set the `error` field of the `Operation` proto. The
    // possible errors include:
    //
    // * `INVALID_ARGUMENT`: One or more arguments are invalid.
    // * `FAILED_PRECONDITION`: One or more errors occurred in setting up the
    //   action requested, such as a missing input or command or no worker being
    //   available. The client may be able to fix the errors and retry.
    // * `RESOURCE_EXHAUSTED`: There is insufficient quota of some resource to
    // run
    //   the action.
    // * `UNAVAILABLE`: Due to a transient condition, such as all workers being
    //   occupied (and the server does not support a queue), the action could
    //   not be started. The client should retry.
    // * `INTERNAL`: An internal error occurred in the execution engine or the
    //   worker.
    // * `DEADLINE_EXCEEDED`: The execution timed out.
    // * `CANCELLED`: The operation was cancelled by the client. This status is
    //   only possible if the server implements the Operations API
    //   CancelOperation method, and it was called for the current execution.
    //
    // In the case of a missing input or command, the server SHOULD additionally
    // send a [PreconditionFailure][google.rpc.PreconditionFailure] error detail
    // where, for each requested blob not present in the CAS, there is a
    // `Violation` with a `type` of `MISSING` and a `subject` of
    // `"blobs/{hash}/{size}"` indicating the digest of the missing blob.
    auto Execute(::grpc::ServerContext* context,
                 const ::bazel_re::ExecuteRequest* request,
                 ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
        -> ::grpc::Status override;

    // Wait for an execution operation to complete. When the client initially
    // makes the request, the server immediately responds with the current
    // status of the execution. The server will leave the request stream open
    // until the operation completes, and then respond with the completed
    // operation. The server MAY choose to stream additional updates as
    // execution progresses, such as to provide an update as to the state of the
    // execution.
    auto WaitExecution(
        ::grpc::ServerContext* context,
        const ::bazel_re::WaitExecutionRequest* request,
        ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
        -> ::grpc::Status override;

  private:
    [[nodiscard]] auto GetAction(::bazel_re::ExecuteRequest const* request)
        const noexcept -> std::pair<std::optional<::bazel_re::Action>,
                                    std::optional<std::string>>;
    [[nodiscard]] auto GetCommand(::bazel_re::Action const& action)
        const noexcept -> std::pair<std::optional<::bazel_re::Command>,
                                    std::optional<std::string>>;

    [[nodiscard]] auto GetIExecutionAction(
        ::bazel_re::ExecuteRequest const* request,
        ::bazel_re::Action const& action) const
        -> std::pair<std::optional<IExecutionAction::Ptr>,
                     std::optional<std::string>>;

    [[nodiscard]] auto GetResponse(
        ::bazel_re::ExecuteRequest const* request,
        IExecutionResponse::Ptr const& i_execution_response) const noexcept
        -> std::pair<std::optional<::bazel_re::ExecuteResponse>,
                     std::optional<std::string>>;

    [[nodiscard]] auto WriteResponse(
        ::bazel_re::ExecuteRequest const* request,
        IExecutionResponse::Ptr const& i_execution_response,
        ::bazel_re::Action const& action,
        ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
        const noexcept -> std::optional<std::string>;

    [[nodiscard]] auto AddResult(
        ::bazel_re::ExecuteResponse* response,
        IExecutionResponse::Ptr const& i_execution_response,
        std::string const& hash) const noexcept -> std::optional<std::string>;
    LocalStorage storage_{};
    IExecutionApi::Ptr api_{new LocalApi()};
    Logger logger_{"execution-service"};
};

#endif
