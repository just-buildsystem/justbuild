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

#include <utility>  // std::move

#include <grpcpp/grpcpp.h>

#include "fmt/core.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/rpc/status.pb.h"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/common/remote/retry.hpp"
#include "src/buildtool/logging/log_level.hpp"

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
            logger->Emit(LogLevel::Debug,
                         "Execution could not be started.\n{}",
                         s.ShortDebugString());
            break;
        case grpc::StatusCode::FAILED_PRECONDITION:
            // quote from remote_execution.proto:
            // One or more errors occurred in setting up the
            // action requested, such as a missing input or command or no worker
            // being available. The client may be able to fix the errors and
            // retry.
            logger->Emit(LogLevel::Progress,
                         "Some precondition for the action failed.\n{}",
                         s.message());
            break;

        default:
            // fallback to default status logging
            LogStatus(logger, LogLevel::Error, s);
            break;
    }
}

auto DebugString(grpc::Status const& status) -> std::string {
    return fmt::format("{}: {}",
                       static_cast<int>(status.error_code()),
                       status.error_message());
}

}  // namespace

BazelExecutionClient::BazelExecutionClient(
    std::string const& server,
    Port port,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RetryConfig const*> const& retry_config) noexcept
    : retry_config_{*retry_config} {
    stub_ = bazel_re::Execution::NewStub(
        CreateChannelWithCredentials(server, port, auth));
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
    (*request.mutable_action_digest()) = action_digest;
    request.set_allocated_execution_policy(execution_policy.release());
    request.set_allocated_results_cache_policy(results_cache_policy.release());
    BazelExecutionClient::ExecutionResponse response;
    auto execute = [this, &request, wait, &response]() -> RetryResponse {
        grpc::ClientContext context;
        std::unique_ptr<grpc::ClientReader<google::longrunning::Operation>>
            reader(stub_->Execute(&context, request));

        auto [op, fatal, _] = ReadExecution(reader.get(), wait);
        if (not op.has_value()) {
            return {.ok = false, .exit_retry_loop = fatal};
        }
        auto contents = ExtractContents(std::move(op));
        response = contents.response;
        if (response.state == ExecutionResponse::State::Ongoing) {
            return {.ok = true, .exit_retry_loop = true};
        }
        if (response.state == ExecutionResponse::State::Finished) {
            return {.ok = true};
        }
        auto const is_fatal = response.state != ExecutionResponse::State::Retry;
        return {.ok = false,
                .exit_retry_loop = is_fatal,
                .error_msg = is_fatal ? std::nullopt : contents.error_msg};
    };
    if (not WithRetry(execute, retry_config_, logger_)) {
        logger_.Emit(LogLevel::Error,
                     "Failed to execute action {}.",
                     action_digest.ShortDebugString());
    }
    return response;
}

auto BazelExecutionClient::WaitExecution(std::string const& execution_handle)
    -> BazelExecutionClient::ExecutionResponse {
    bazel_re::WaitExecutionRequest request;
    request.set_name(execution_handle);

    BazelExecutionClient::ExecutionResponse response;

    auto wait_execution = [this, &request, &response]() -> RetryResponse {
        grpc::ClientContext context;
        std::unique_ptr<grpc::ClientReader<google::longrunning::Operation>>
            reader(stub_->WaitExecution(&context, request));

        auto [op, fatal, _] = ReadExecution(reader.get(), /*wait=*/true);
        if (not op.has_value()) {
            return {.ok = false, .exit_retry_loop = fatal};
        }
        auto contents = ExtractContents(std::move(op));
        response = contents.response;
        if (response.state == ExecutionResponse::State::Finished) {
            return {.ok = true};
        }
        auto const is_fatal = response.state != ExecutionResponse::State::Retry;
        return {.ok = false,
                .exit_retry_loop = is_fatal,
                .error_msg = is_fatal ? std::nullopt : contents.error_msg};
    };
    if (not WithRetry(wait_execution, retry_config_, logger_)) {
        logger_.Emit(
            LogLevel::Error, "Failed to Execute action {}.", request.name());
    }
    return response;
}

auto BazelExecutionClient::ReadExecution(
    grpc::ClientReader<google::longrunning::Operation>* reader,
    bool wait) -> RetryReadOperation {
    if (reader == nullptr) {
        grpc::Status status{grpc::StatusCode::UNKNOWN, "Reader unavailable"};
        LogStatus(&logger_, LogLevel::Error, status);
        return {.operation = std::nullopt,
                .exit_retry_loop = true,
                .error_msg = DebugString(status)};
    }

    google::longrunning::Operation operation;
    if (not reader->Read(&operation)) {
        grpc::Status status = reader->Finish();
        auto exit_retry_loop =
            (status.error_code() != grpc::StatusCode::UNAVAILABLE) &&
            (status.error_code() != grpc::StatusCode::DEADLINE_EXCEEDED);
        LogStatus(&logger_,
                  (exit_retry_loop ? LogLevel::Error : LogLevel::Debug),
                  status);
        return {std::nullopt, exit_retry_loop, DebugString(status)};
    }
    // Important note: do not call reader->Finish() unless reader->Read()
    // returned false, otherwise the thread will be never released
    if (wait) {
        while (reader->Read(&operation)) {
        }
        grpc::Status status = reader->Finish();
        if (not status.ok()) {
            auto exit_retry_loop =
                (status.error_code() != grpc::StatusCode::UNAVAILABLE) &&
                (status.error_code() != grpc::StatusCode::DEADLINE_EXCEEDED);
            LogStatus(&logger_,
                      (exit_retry_loop ? LogLevel::Error : LogLevel::Debug),
                      status);
            return {std::nullopt, exit_retry_loop, DebugString(status)};
        }
    }
    return {.operation = operation, .exit_retry_loop = false};
}

auto BazelExecutionClient::ExtractContents(
    std::optional<google::longrunning::Operation>&& operation)
    -> RetryExtractContents {
    if (not operation) {
        // Error was already logged in ReadExecution()
        return {ExecutionResponse::MakeEmptyFailed(), std::nullopt};
    }
    ExecutionResponse response;
    response.execution_handle = operation->name();
    if (not operation->done()) {
        response.state = ExecutionResponse::State::Ongoing;
        return {response, std::nullopt};
    }
    if (operation->has_error()) {
        LogStatus(&logger_, LogLevel::Debug, operation->error());
        if (operation->error().code() == grpc::StatusCode::UNAVAILABLE) {
            response.state = ExecutionResponse::State::Retry;
        }
        else {
            response.state = ExecutionResponse::State::Failed;
        }
        return {response, operation->error().ShortDebugString()};
    }

    // Get execution response Unpacked from Protobufs Any type to the actual
    // type in our case
    auto const& raw_response = operation->response();
    if (not raw_response.Is<bazel_re::ExecuteResponse>()) {
        // Fatal error, the type should be correct
        logger_.Emit(LogLevel::Error, "Corrupted ExecuteResponse");
        response.state = ExecutionResponse::State::Failed;
        return {response, "Corrupted ExecuteResponse"};
    }

    bazel_re::ExecuteResponse exec_response;
    raw_response.UnpackTo(&exec_response);
    auto status_code = exec_response.status().code();
    if (status_code != grpc::StatusCode::OK) {
        LogExecutionStatus(&logger_, exec_response.status());
        if (status_code == grpc::StatusCode::UNAVAILABLE) {
            response.state = ExecutionResponse::State::Retry;
        }
        else if (status_code == grpc::StatusCode::FAILED_PRECONDITION) {
            logger_.Emit(LogLevel::Debug, [&exec_response] {
                std::string text_repr;
                google::protobuf::TextFormat::PrintToString(exec_response,
                                                            &text_repr);
                return fmt::format(
                    "Full exec_response of precondition failure\n{}",
                    text_repr);
            });
            response.state = ExecutionResponse::State::Retry;
        }
        else {
            response.state = ExecutionResponse::State::Failed;
        }
        return {response, exec_response.status().ShortDebugString()};
    }

    ExecutionOutput output;
    output.action_result = exec_response.result();
    output.cached_result = exec_response.cached_result();
    output.message = exec_response.message();
    for (const auto& [k, v] : exec_response.server_logs()) {
        output.server_logs[k].CopyFrom(v);
    }
    auto metadata = exec_response.result().execution_metadata();
    auto const& start_time = metadata.worker_start_timestamp();
    auto const& stop_time = metadata.worker_completed_timestamp();
    const double k_one_nanosecond_in_seconds = 1e-9;
    output.duration =
        static_cast<double>(stop_time.seconds() - start_time.seconds()) +
        k_one_nanosecond_in_seconds *
            static_cast<double>(stop_time.nanos() - start_time.nanos());
    response.output = output;
    response.state = ExecutionResponse::State::Finished;

    return {response, std::nullopt};
}
