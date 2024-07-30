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
#include <string>
#include <utility>

#include "execution_server.hpp"
#include "fmt/core.h"
#include "src/buildtool/execution_api/execution_service/operation_cache.hpp"
#include "src/buildtool/execution_api/local/local_cas_reader.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/verify_hash.hpp"

static void UpdateTimeStamp(::google::longrunning::Operation* op) {
    ::google::protobuf::Timestamp t;
    t.set_seconds(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch())
            .count());
    op->mutable_metadata()->PackFrom(t);
}

auto ExecutionServiceImpl::GetAction(::bazel_re::ExecuteRequest const* request)
    const noexcept -> expected<::bazel_re::Action, std::string> {
    // get action description
    if (auto error_msg = IsAHash(request->action_digest().hash())) {
        logger_.Emit(LogLevel::Error, "{}", *error_msg);
        return unexpected{std::move(*error_msg)};
    }
    auto path = storage_.CAS().BlobPath(request->action_digest(), false);
    if (not path) {
        auto str = fmt::format("could not retrieve blob {} from cas",
                               request->action_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return unexpected{std::move(str)};
    }

    ::bazel_re::Action action{};
    if (std::ifstream f(*path); not action.ParseFromIstream(&f)) {
        auto str = fmt::format("failed to parse action from blob {}",
                               request->action_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return unexpected{std::move(str)};
    }
    if (auto error_msg = IsAHash(action.input_root_digest().hash())) {
        logger_.Emit(LogLevel::Error, "{}", *error_msg);
        return unexpected{*std::move(error_msg)};
    }
    path = Compatibility::IsCompatible()
               ? storage_.CAS().BlobPath(action.input_root_digest(), false)
               : storage_.CAS().TreePath(action.input_root_digest());

    if (not path) {
        auto str = fmt::format("could not retrieve input root {} from cas",
                               action.input_root_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return unexpected{std::move(str)};
    }
    return std::move(action);
}

auto ExecutionServiceImpl::GetCommand(::bazel_re::Action const& action)
    const noexcept -> expected<::bazel_re::Command, std::string> {
    if (auto error_msg = IsAHash(action.command_digest().hash())) {
        logger_.Emit(LogLevel::Error, "{}", *error_msg);
        return unexpected{*std::move(error_msg)};
    }
    auto path = storage_.CAS().BlobPath(action.command_digest(), false);
    if (not path) {
        auto str = fmt::format("could not retrieve blob {} from cas",
                               action.command_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return unexpected{std::move(str)};
    }

    ::bazel_re::Command c{};
    if (std::ifstream f(*path); not c.ParseFromIstream(&f)) {
        auto str = fmt::format("failed to parse command from blob {}",
                               action.command_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return unexpected{std::move(str)};
    }
    return std::move(c);
}

static auto GetEnvVars(::bazel_re::Command const& c)
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
    ::bazel_re::ExecuteRequest const* request,
    ::bazel_re::Action const& action) const
    -> expected<IExecutionAction::Ptr, std::string> {
    auto c = GetCommand(action);
    if (not c) {
        return unexpected{std::move(c).error()};
    }

    auto env_vars = GetEnvVars(*c);

    auto i_execution_action = api_.CreateAction(
        ArtifactDigest{action.input_root_digest()},
        {c->arguments().begin(), c->arguments().end()},
        c->working_directory(),
        {c->output_files().begin(), c->output_files().end()},
        {c->output_directories().begin(), c->output_directories().end()},
        env_vars,
        {});
    if (not i_execution_action) {
        auto str = fmt::format("could not create action from {}",
                               request->action_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return unexpected{std::move(str)};
    }
    i_execution_action->SetCacheFlag(
        action.do_not_cache() ? IExecutionAction::CacheFlag::DoNotCacheOutput
                              : IExecutionAction::CacheFlag::CacheOutput);
    return std::move(i_execution_action);
}

static auto CreateTreeDigestFromDirectoryDigest(
    ::bazel_re::Digest const& dir_digest,
    Storage const& storage) noexcept -> std::optional<::bazel_re::Digest> {
    LocalCasReader reader(&storage.CAS());
    if (auto tree = reader.MakeTree(ArtifactDigest{dir_digest})) {
        // serialize and store tree message
        auto content = tree->SerializeAsString();
        return storage.CAS().StoreBlob(content, /*is_executable=*/false);
    }
    return std::nullopt;
}

static auto AddOutputPaths(::bazel_re::ExecuteResponse* response,
                           IExecutionResponse::Ptr const& execution,
                           Storage const& storage) noexcept -> bool {
    auto const& artifacts = execution->Artifacts();
    auto const& dir_symlinks = execution->DirectorySymlinks();
    auto const size = static_cast<int>(artifacts.size());
    response->mutable_result()->mutable_output_files()->Reserve(size);
    response->mutable_result()->mutable_output_file_symlinks()->Reserve(size);
    response->mutable_result()->mutable_output_directory_symlinks()->Reserve(
        size);
    response->mutable_result()->mutable_output_directories()->Reserve(size);

    for (auto const& [path, info] : artifacts) {
        auto dgst = static_cast<::bazel_re::Digest>(info.digest);

        if (info.type == ObjectType::Tree) {
            ::bazel_re::OutputDirectory out_dir;
            *(out_dir.mutable_path()) = path;
            if (not Compatibility::IsCompatible()) {
                // In native mode: Set the directory digest directly.
                *(out_dir.mutable_tree_digest()) = std::move(dgst);
            }
            else {
                // In compatible mode: Create a tree digest from directory
                // digest on the fly and set tree digest.
                auto digest =
                    CreateTreeDigestFromDirectoryDigest(dgst, storage);
                if (not digest) {
                    return false;
                }
                *(out_dir.mutable_tree_digest()) = std::move(*digest);
            }
            response->mutable_result()->mutable_output_directories()->Add(
                std::move(out_dir));
        }
        else if (info.type == ObjectType::Symlink) {
            ::bazel_re::OutputSymlink out_link;
            *(out_link.mutable_path()) = path;
            // recover the target of the symlink
            auto cas_path =
                storage.CAS().BlobPath(dgst, /*is_executable=*/false);
            if (not cas_path) {
                return false;
            }
            auto const& content = FileSystemManager::ReadFile(*cas_path);
            if (not content) {
                return false;
            }
            *(out_link.mutable_target()) = *content;
            if (dir_symlinks.contains(path)) {
                // directory symlink
                response->mutable_result()
                    ->mutable_output_directory_symlinks()
                    ->Add(std::move(out_link));
            }
            else {
                // file symlinks
                response->mutable_result()->mutable_output_file_symlinks()->Add(
                    std::move(out_link));
            }
        }
        else {
            ::bazel_re::OutputFile out_file;
            *(out_file.mutable_path()) = path;
            *(out_file.mutable_digest()) = std::move(dgst);
            out_file.set_is_executable(info.type == ObjectType::Executable);
            response->mutable_result()->mutable_output_files()->Add(
                std::move(out_file));
        }
    }
    return true;
}

auto ExecutionServiceImpl::AddResult(
    ::bazel_re::ExecuteResponse* response,
    IExecutionResponse::Ptr const& i_execution_response,
    std::string const& action_hash) const noexcept
    -> expected<std::monostate, std::string> {
    if (not AddOutputPaths(response, i_execution_response, storage_)) {
        auto str = fmt::format("Error in creating output paths of action {}",
                               action_hash);
        logger_.Emit(LogLevel::Error, "{}", str);
        return unexpected{std::move(str)};
    }
    auto* result = response->mutable_result();
    result->set_exit_code(i_execution_response->ExitCode());
    if (i_execution_response->HasStdErr()) {
        auto dgst = storage_.CAS().StoreBlob(i_execution_response->StdErr(),
                                             /*is_executable=*/false);
        if (not dgst) {
            auto str =
                fmt::format("Could not store stderr of action {}", action_hash);
            logger_.Emit(LogLevel::Error, "{}", str);
            return unexpected{std::move(str)};
        }
        result->mutable_stderr_digest()->CopyFrom(*dgst);
    }
    if (i_execution_response->HasStdOut()) {
        auto dgst = storage_.CAS().StoreBlob(i_execution_response->StdOut(),
                                             /*is_executable=*/false);
        if (not dgst) {
            auto str =
                fmt::format("Could not store stdout of action {}", action_hash);
            logger_.Emit(LogLevel::Error, "{}", str);
            return unexpected{std::move(str)};
        }
        result->mutable_stdout_digest()->CopyFrom(*dgst);
    }
    return std::monostate{};
}

static void AddStatus(::bazel_re::ExecuteResponse* response) noexcept {
    ::google::rpc::Status status{};
    // we run the action locally, so no communication issues should happen
    status.set_code(grpc::StatusCode::OK);
    *(response->mutable_status()) = status;
}

auto ExecutionServiceImpl::GetResponse(
    ::bazel_re::ExecuteRequest const* request,
    IExecutionResponse::Ptr const& i_execution_response) const noexcept
    -> expected<::bazel_re::ExecuteResponse, std::string> {
    ::bazel_re::ExecuteResponse response{};
    AddStatus(&response);
    auto result = AddResult(
        &response, i_execution_response, request->action_digest().hash());
    if (not result) {
        return unexpected{std::move(result).error()};
    }
    response.set_cached_result(i_execution_response->IsCached());
    return std::move(response);
}

auto ExecutionServiceImpl::StoreActionResult(
    ::bazel_re::ExecuteRequest const* request,
    IExecutionResponse::Ptr const& i_execution_response,
    ::bazel_re::ExecuteResponse const& execute_response,
    ::bazel_re::Action const& action) const noexcept
    -> expected<std::monostate, std::string> {
    if (i_execution_response->ExitCode() == 0 and not action.do_not_cache() and
        not storage_.ActionCache().StoreResult(request->action_digest(),
                                               execute_response.result())) {
        auto str = fmt::format("Could not store action result for action {}",
                               request->action_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return unexpected{std::move(str)};
    }
    return std::monostate{};
}
void ExecutionServiceImpl::WriteResponse(
    ::bazel_re::ExecuteResponse const& execute_response,
    ::grpc::ServerWriter<::google::longrunning::Operation>* writer,
    ::google::longrunning::Operation* op) noexcept {

    // send response to the client
    op->mutable_response()->PackFrom(execute_response);
    op->set_done(true);
    UpdateTimeStamp(op);

    op_cache_.Set(op->name(), *op);
    writer->Write(*op);
}

auto ExecutionServiceImpl::Execute(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::ExecuteRequest* request,
    ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
    -> ::grpc::Status {
    auto lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        auto str = fmt::format("Could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    auto action = GetAction(request);
    if (not action) {
        return ::grpc::Status{grpc::StatusCode::INTERNAL,
                              std::move(action).error()};
    }
    auto i_execution_action = GetIExecutionAction(request, *action);
    if (not i_execution_action) {
        return ::grpc::Status{grpc::StatusCode::INTERNAL,
                              std::move(i_execution_action).error()};
    }

    logger_.Emit(LogLevel::Info, "Execute {}", request->action_digest().hash());
    // send initial response to the client
    auto op = ::google::longrunning::Operation{};
    auto const& op_name = request->action_digest().hash();
    op.set_name(op_name);
    op.set_done(false);
    UpdateTimeStamp(&op);
    op_cache_.Set(op_name, op);
    writer->Write(op);
    auto t0 = std::chrono::high_resolution_clock::now();
    auto i_execution_response = i_execution_action->get()->Execute(&logger_);
    auto t1 = std::chrono::high_resolution_clock::now();
    logger_.Emit(
        LogLevel::Trace,
        "Finished execution of {} in {} seconds",
        request->action_digest().hash(),
        std::chrono::duration_cast<std::chrono::seconds>(t1 - t0).count());

    auto execute_response = GetResponse(request, i_execution_response);
    if (not execute_response) {
        return ::grpc::Status{grpc::StatusCode::INTERNAL,
                              std::move(execute_response).error()};
    }

    auto store_result = StoreActionResult(
        request, i_execution_response, *execute_response, *action);
    if (not store_result) {
        return ::grpc::Status{grpc::StatusCode::INTERNAL,
                              std::move(store_result).error()};
    }
    WriteResponse(*execute_response, writer, &op);
    return ::grpc::Status::OK;
}

auto ExecutionServiceImpl::WaitExecution(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::WaitExecutionRequest* request,
    ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
    -> ::grpc::Status {
    auto const& hash = request->name();
    if (auto error_msg = IsAHash(hash)) {
        logger_.Emit(LogLevel::Error, "{}", *error_msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *error_msg};
    }
    logger_.Emit(LogLevel::Trace, "WaitExecution: {}", hash);
    std::optional<::google::longrunning::Operation> op;
    do {
        op = op_cache_.Query(hash);
        if (not op) {
            auto const& str = fmt::format(
                "Executing action {} not found in internal cache.", hash);
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } while (not op->done());
    writer->Write(*op);
    logger_.Emit(LogLevel::Trace, "Finished WaitExecution {}", hash);
    return ::grpc::Status::OK;
}
