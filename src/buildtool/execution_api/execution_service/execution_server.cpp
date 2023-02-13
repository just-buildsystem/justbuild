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

#include "fmt/format.h"
#include "src/buildtool/execution_api/local/garbage_collector.hpp"

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
    auto path = storage_.BlobPath(request->action_digest(), false);
    if (!path) {
        auto const& str = fmt::format("could not retrieve blob {} from cas",
                                      request->action_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    ::build::bazel::remote::execution::v2::Action a{};
    {
        std::ifstream f(*path);
        if (!a.ParseFromIstream(&f)) {
            auto const& str = fmt::format("failed to parse action from blob {}",
                                          request->action_digest().hash());
            logger_.Emit(LogLevel::Error, str);
            return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
        }
    }
    path = storage_.BlobPath(a.command_digest(), false);
    if (!path) {
        auto const& str = fmt::format("could not retrieve blob {} from cas",
                                      a.command_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    ::build::bazel::remote::execution::v2::Command c{};
    {
        std::ifstream f(*path);
        if (!c.ParseFromIstream(&f)) {
            auto const& str =
                fmt::format("failed to parse command from blob {}",
                            a.command_digest().hash());
            return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
        }
    }
    path = Compatibility::IsCompatible()
               ? storage_.BlobPath(a.input_root_digest(), false)
               : storage_.TreePath(a.input_root_digest());

    if (!path) {
        auto const& str =
            fmt::format("could not retrieve input root {} from cas",
                        a.input_root_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    auto op = ::google::longrunning::Operation{};
    op.set_name("just-remote-execution");
    std::map<std::string, std::string> env_vars;
    for (auto const& x : c.environment_variables()) {
        env_vars.emplace(x.name(), x.value());
    }
    auto action = api_->CreateAction(
        ArtifactDigest{a.input_root_digest()},
        {c.arguments().begin(), c.arguments().end()},
        {c.output_files().begin(), c.output_files().end()},
        {c.output_directories().begin(), c.output_directories().end()},
        env_vars,
        {});
    action->SetCacheFlag(a.do_not_cache()
                             ? IExecutionAction::CacheFlag::DoNotCacheOutput
                             : IExecutionAction::CacheFlag::CacheOutput);

    logger_.Emit(LogLevel::Info, "Execute {}", request->action_digest().hash());
    auto tmp = action->Execute(&logger_);
    logger_.Emit(LogLevel::Trace,
                 "Finished execution of {}",
                 request->action_digest().hash());
    ::build::bazel::remote::execution::v2::ExecuteResponse response{};
    for (auto const& [path, info] : tmp->Artifacts()) {
        if (info.type == ObjectType::Tree) {
            auto* d = response.mutable_result()->add_output_directories();
            *(d->mutable_path()) = path;
            *(d->mutable_tree_digest()) =
                static_cast<::build::bazel::remote::execution::v2::Digest>(
                    info.digest);
        }
        else {
            auto* f = response.mutable_result()->add_output_files();
            *(f->mutable_path()) = path;
            *(f->mutable_digest()) =
                static_cast<::build::bazel::remote::execution::v2::Digest>(
                    info.digest);
            if (info.type == ObjectType::Executable) {
                f->set_is_executable(true);
            }
        }
    }
    response.set_cached_result(tmp->IsCached());

    op.set_done(true);
    ::google::rpc::Status status{};
    *(response.mutable_status()) = status;
    auto* result = response.mutable_result();
    result->set_exit_code(tmp->ExitCode());
    if (tmp->HasStdErr()) {
        auto dgst = storage_.StoreBlob(tmp->StdErr(), /*is_executable=*/false);
        if (!dgst) {
            auto str = fmt::format("Could not store stderr of action {}",
                                   request->action_digest().hash());
            logger_.Emit(LogLevel::Error, str);
            return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
        }
        result->mutable_stderr_digest()->CopyFrom(*dgst);
    }
    if (tmp->HasStdOut()) {
        auto dgst = storage_.StoreBlob(tmp->StdOut(), /*is_executable=*/false);
        if (!dgst) {
            auto str = fmt::format("Could not store stdout of action {}",
                                   request->action_digest().hash());
            logger_.Emit(LogLevel::Error, str);
            return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
        }
        result->mutable_stdout_digest()->CopyFrom(*dgst);
    }

    op.mutable_response()->PackFrom(response);
    writer->Write(op);
    if (tmp->ExitCode() == 0 && !a.do_not_cache() &&
        !storage_.StoreActionResult(request->action_digest(),
                                    response.result())) {
        auto str = fmt::format("Could not store action result for action {}",
                               request->action_digest().hash());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
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
