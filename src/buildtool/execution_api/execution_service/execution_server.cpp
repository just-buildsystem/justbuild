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

#include "src/buildtool/execution_api/execution_service/execution_server.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#include "fmt/format.h"
#include "src/buildtool/execution_api/local/garbage_collector.hpp"

auto ExecutionServiceImpl::GetAction(
    ::build::bazel::remote::execution::v2::ExecuteRequest const* request)
    const noexcept
    -> std::pair<std::optional<::build::bazel::remote::execution::v2::Action>,
                 std::optional<std::string>> {
    // get action description
    auto path = storage_.BlobPath(request->action_digest(), false);
    if (!path) {
        auto str = fmt::format("could not retrieve blob {} from cas",
                               request->action_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return {std::nullopt, str};
    }
    ::build::bazel::remote::execution::v2::Action action{};
    {
        std::ifstream f(*path);
        if (!action.ParseFromIstream(&f)) {
            auto str = fmt::format("failed to parse action from blob {}",
                                   request->action_digest().hash());
            logger_.Emit(LogLevel::Error, str);
            return {std::nullopt, str};
        }
    }

    path = Compatibility::IsCompatible()
               ? storage_.BlobPath(action.input_root_digest(), false)
               : storage_.TreePath(action.input_root_digest());

    if (!path) {
        auto str = fmt::format("could not retrieve input root {} from cas",
                               action.input_root_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return {std::nullopt, str};
    }
    return {std::move(action), std::nullopt};
}

auto ExecutionServiceImpl::GetCommand(
    ::build::bazel::remote::execution::v2::Action const& action) const noexcept
    -> std::pair<std::optional<::build::bazel::remote::execution::v2::Command>,
                 std::optional<std::string>> {

    auto path = storage_.BlobPath(action.command_digest(), false);
    if (!path) {
        auto str = fmt::format("could not retrieve blob {} from cas",
                               action.command_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return {std::nullopt, str};
    }

    ::build::bazel::remote::execution::v2::Command c{};
    {
        std::ifstream f(*path);
        if (!c.ParseFromIstream(&f)) {
            auto str = fmt::format("failed to parse command from blob {}",
                                   action.command_digest().hash());
            logger_.Emit(LogLevel::Error, str);
            return {std::nullopt, str};
        }
    }
    return {c, std::nullopt};
}

static auto GetEnvVars(::build::bazel::remote::execution::v2::Command const& c)
    -> std::map<std::string, std::string> {
    std::map<std::string, std::string> env_vars{};
    std::transform(c.environment_variables().begin(),
                   c.environment_variables().end(),
                   std::inserter(env_vars, env_vars.begin()),
                   [](auto const& x) -> std::pair<std::string, std::string> {
                       return {x.name(), x.value()};
                   });
    return env_vars;
}

auto ExecutionServiceImpl::GetIExecutionAction(
    ::build::bazel::remote::execution::v2::ExecuteRequest const* request,
    ::build::bazel::remote::execution::v2::Action const& action) const
    -> std::pair<std::optional<IExecutionAction::Ptr>,
                 std::optional<std::string>> {

    auto [c, msg_c] = GetCommand(action);
    if (!c) {
        return {std::nullopt, *msg_c};
    }

    auto env_vars = GetEnvVars(*c);

    auto i_execution_action = api_->CreateAction(
        ArtifactDigest{action.input_root_digest()},
        {c->arguments().begin(), c->arguments().end()},
        {c->output_files().begin(), c->output_files().end()},
        {c->output_directories().begin(), c->output_directories().end()},
        env_vars,
        {});
    if (!i_execution_action) {
        auto str = fmt::format("could not create action from {}",
                               request->action_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return {std::nullopt, str};
    }
    i_execution_action->SetCacheFlag(
        action.do_not_cache() ? IExecutionAction::CacheFlag::DoNotCacheOutput
                              : IExecutionAction::CacheFlag::CacheOutput);
    return {std::move(i_execution_action), std::nullopt};
}

static void AddOutputPaths(
    ::build::bazel::remote::execution::v2::ExecuteResponse* response,
    IExecutionResponse::Ptr const& execution) noexcept {
    auto const& size = static_cast<int>(execution->Artifacts().size());
    response->mutable_result()->mutable_output_files()->Reserve(size);
    response->mutable_result()->mutable_output_directories()->Reserve(size);

    for (auto const& [path, info] : execution->Artifacts()) {
        auto dgst = static_cast<::build::bazel::remote::execution::v2::Digest>(
            info.digest);

        if (info.type == ObjectType::Tree) {
            ::build::bazel::remote::execution::v2::OutputDirectory out_dir;
            *(out_dir.mutable_path()) = path;
            *(out_dir.mutable_tree_digest()) = std::move(dgst);
            response->mutable_result()->mutable_output_directories()->Add(
                std::move(out_dir));
        }
        else {
            ::build::bazel::remote::execution::v2::OutputFile out_file;
            *(out_file.mutable_path()) = path;
            *(out_file.mutable_digest()) = std::move(dgst);
            out_file.set_is_executable(info.type == ObjectType::Executable);
            response->mutable_result()->mutable_output_files()->Add(
                std::move(out_file));
        }
    }
}

auto ExecutionServiceImpl::AddResult(
    ::build::bazel::remote::execution::v2::ExecuteResponse* response,
    IExecutionResponse::Ptr const& i_execution_response,
    std::string const& hash) const noexcept -> std::optional<std::string> {
    AddOutputPaths(response, i_execution_response);
    auto* result = response->mutable_result();
    result->set_exit_code(i_execution_response->ExitCode());
    if (i_execution_response->HasStdErr()) {
        auto dgst = storage_.StoreBlob(i_execution_response->StdErr(),
                                       /*is_executable=*/false);
        if (!dgst) {
            auto str = fmt::format("Could not store stderr of action {}", hash);
            logger_.Emit(LogLevel::Error, str);
            return str;
        }
        result->mutable_stderr_digest()->CopyFrom(*dgst);
    }
    if (i_execution_response->HasStdOut()) {
        auto dgst = storage_.StoreBlob(i_execution_response->StdOut(),
                                       /*is_executable=*/false);
        if (!dgst) {
            auto str = fmt::format("Could not store stdout of action {}", hash);
            logger_.Emit(LogLevel::Error, str);
            return str;
        }
        result->mutable_stdout_digest()->CopyFrom(*dgst);
    }
    return std::nullopt;
}

static void AddStatus(
    ::build::bazel::remote::execution::v2::ExecuteResponse* response) noexcept {
    ::google::rpc::Status status{};
    // we run the action locally, so no communication issues should happen
    status.set_code(grpc::StatusCode::OK);
    *(response->mutable_status()) = status;
}

auto ExecutionServiceImpl::GetResponse(
    ::build::bazel::remote::execution::v2::ExecuteRequest const* request,
    IExecutionResponse::Ptr const& i_execution_response) const noexcept
    -> std::pair<
        std::optional<::build::bazel::remote::execution::v2::ExecuteResponse>,
        std::optional<std::string>> {

    ::build::bazel::remote::execution::v2::ExecuteResponse response{};
    AddStatus(&response);
    auto err = AddResult(
        &response, i_execution_response, request->action_digest().hash());
    if (err) {
        return {std::nullopt, *err};
    }
    response.set_cached_result(i_execution_response->IsCached());
    return {response, std::nullopt};
}

auto ExecutionServiceImpl::WriteResponse(
    ::build::bazel::remote::execution::v2::ExecuteRequest const* request,
    IExecutionResponse::Ptr const& i_execution_response,
    ::build::bazel::remote::execution::v2::Action const& action,
    ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
    const noexcept -> std::optional<std::string> {

    auto [execute_response, msg_r] = GetResponse(request, i_execution_response);
    if (!execute_response) {

        return *msg_r;
    }

    // store action result
    if (i_execution_response->ExitCode() == 0 && !action.do_not_cache() &&
        !storage_.StoreActionResult(request->action_digest(),
                                    execute_response->result())) {
        auto str = fmt::format("Could not store action result for action {}",
                               request->action_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return str;
    }

    // send response to the client
    auto op = ::google::longrunning::Operation{};
    op.mutable_response()->PackFrom(*execute_response);
    op.set_name("just-remote-execution");
    op.set_done(true);
    if (!writer->Write(op)) {
        auto str =
            fmt::format("Could not write execution response for action {}",
                        request->action_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return str;
    }
    return std::nullopt;
}

auto ExecutionServiceImpl::Execute(
    ::grpc::ServerContext* /*context*/,
    const ::build::bazel::remote::execution::v2::ExecuteRequest* request,
    ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
    -> ::grpc::Status {

    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    auto [action, msg_a] = GetAction(request);
    if (!action) {
        return ::grpc::Status{grpc::StatusCode::INTERNAL, *msg_a};
    }
    auto [i_execution_action, msg] = GetIExecutionAction(request, *action);
    if (!i_execution_action) {
        return ::grpc::Status{grpc::StatusCode::INTERNAL, *msg};
    }

    logger_.Emit(LogLevel::Info, "Execute {}", request->action_digest().hash());
    auto i_execution_response = i_execution_action->get()->Execute(&logger_);
    logger_.Emit(LogLevel::Trace,
                 "Finished execution of {}",
                 request->action_digest().hash());
    auto err = WriteResponse(request, i_execution_response, *action, writer);
    if (err) {
        return ::grpc::Status{grpc::StatusCode::INTERNAL, *err};
    }
    return ::grpc::Status::OK;
}

auto ExecutionServiceImpl::WaitExecution(
    ::grpc::ServerContext* /*context*/,
    const ::build::bazel::remote::execution::v2::
        WaitExecutionRequest* /*request*/,
    ::grpc::ServerWriter<::google::longrunning::Operation>* /*writer*/)
    -> ::grpc::Status {
    auto const* str = "WaitExecution not implemented";
    logger_.Emit(LogLevel::Error, str);
    return ::grpc::Status{grpc::StatusCode::UNIMPLEMENTED, str};
}
