// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"

#include "grpcpp/grpcpp.h"
#include "gsl/gsl"
#include "src/buildtool/common/remote/client_common.hpp"

namespace bazel_re = build::bazel::remote::execution::v2;

namespace {

void LogExecutionStatus(gsl::not_null<Logger const*> const& logger,
                        google::rpc::Status const& s) noexcept {
    switch (s.code()) {
        case grpc::StatusCode::DEADLINE_EXCEEDED:
            logger->Emit(LogLevel::Error, "Execution timed out.");
            break;
        case grpc::StatusCode::UNAVAILABLE:
            // quote from remote_execution.proto:
            // Due to a transient condition, such as all workers being occupied
            // (and the server does not support a queue), the action could not
            // be started. The client should retry.
            logger->Emit(LogLevel::Error,
                         fmt::format("Execution could not be started.\n{}",
                                     s.DebugString()));
            break;
        default:
            // fallback to default status logging
            LogStatus(logger, LogLevel::Warning, s);
            break;
    }
}

}  // namespace

BazelExecutionClient::BazelExecutionClient(std::string const& server,
                                           Port port) noexcept {
    stub_ = bazel_re::Execution::NewStub(
        CreateChannelWithCredentials(server, port));
}

auto BazelExecutionClient::Execute(std::string const& instance_name,
                                   bazel_re::Digest const& action_digest,
                                   ExecutionConfiguration const& config,
                                   bool wait)
    -> BazelExecutionClient::ExecutionResponse {
    auto execution_policy = std::make_unique<bazel_re::ExecutionPolicy>();
    execution_policy->set_priority(config.execution_priority);

    auto results_cache_policy =
        std::make_unique<bazel_re::ResultsCachePolicy>();
    results_cache_policy->set_priority(config.results_cache_priority);

    bazel_re::ExecuteRequest request;
    request.set_instance_name(instance_name);
    request.set_skip_cache_lookup(config.skip_cache_lookup);
    request.set_allocated_action_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest(action_digest)});
    request.set_allocated_execution_policy(execution_policy.release());
    request.set_allocated_results_cache_policy(results_cache_policy.release());

    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientReader<google::longrunning::Operation>> reader(
        stub_->Execute(&context, request));

    return ExtractContents(ReadExecution(reader.get(), wait));
}

auto BazelExecutionClient::WaitExecution(std::string const& execution_handle)
    -> BazelExecutionClient::ExecutionResponse {
    bazel_re::WaitExecutionRequest request;
    request.set_name(execution_handle);

    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientReader<google::longrunning::Operation>> reader(
        stub_->WaitExecution(&context, request));

    return ExtractContents(ReadExecution(reader.get(), true));
}

auto BazelExecutionClient::ReadExecution(
    grpc::ClientReader<google::longrunning::Operation>* reader,
    bool wait) -> std::optional<google::longrunning::Operation> {
    if (reader == nullptr) {
        LogStatus(
            &logger_,
            LogLevel::Error,
            grpc::Status{grpc::StatusCode::UNKNOWN, "Reader unavailable"});
        return std::nullopt;
    }

    google::longrunning::Operation operation;
    if (not reader->Read(&operation)) {
        grpc::Status status = reader->Finish();
        // TODO(vmoreno): log error using data in status and operation
        LogStatus(&logger_, LogLevel::Error, status);
        return std::nullopt;
    }
    // Important note: do not call reader->Finish() unless reader->Read()
    // returned false, otherwise the thread will be never released
    if (wait) {
        while (reader->Read(&operation)) {
        }
        grpc::Status status = reader->Finish();
        if (not status.ok()) {
            // TODO(vmoreno): log error from status and operation
            LogStatus(&logger_, LogLevel::Error, status);
            return std::nullopt;
        }
    }
    return operation;
}

auto BazelExecutionClient::ExtractContents(
    std::optional<google::longrunning::Operation>&& operation)
    -> BazelExecutionClient::ExecutionResponse {
    if (not operation) {
        // Error was already logged in ReadExecution()
        return ExecutionResponse::MakeEmptyFailed();
    }
    auto op = *operation;
    ExecutionResponse response;
    response.execution_handle = op.name();
    if (not op.done()) {
        response.state = ExecutionResponse::State::Ongoing;
        return response;
    }
    if (op.has_error()) {
        // TODO(vmoreno): log error from google::rpc::Status s = op.error()
        LogStatus(&logger_, LogLevel::Debug, op.error());
        response.state = ExecutionResponse::State::Failed;
        return response;
    }

    // Get execution response Unpacked from Protobufs Any type to the actual
    // type in our case
    auto const& raw_response = op.response();
    if (not raw_response.Is<bazel_re::ExecuteResponse>()) {
        // Fatal error, the type should be correct
        response.state = ExecutionResponse::State::Failed;
        return response;
    }

    bazel_re::ExecuteResponse exec_response;
    raw_response.UnpackTo(&exec_response);

    if (exec_response.status().code() != grpc::StatusCode::OK) {
        // For now, treat all execution errors (e.g., action timeout) as fatal.
        LogExecutionStatus(&logger_, exec_response.status());
        response.state = ExecutionResponse::State::Failed;
        return response;
    }

    ExecutionOutput output;
    output.action_result = exec_response.result();
    output.cached_result = exec_response.cached_result();
    output.message = exec_response.message();
    output.server_logs = exec_response.server_logs();
    response.output = output;
    response.state = ExecutionResponse::State::Finished;

    return response;
}
