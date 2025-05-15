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

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/rpc/status.pb.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/execution_service/operation_cache.hpp"
#include "src/buildtool/execution_api/local/local_cas_reader.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {
void SetTimeStamp(
    gsl::not_null<::google::protobuf::Timestamp*> const& t,
    std::chrono::time_point<std::chrono::high_resolution_clock> const& tvalue) {
    const int64_t k_nanoseconds_per_second = 1'000'000'000;
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     tvalue.time_since_epoch())
                     .count();
    t->set_seconds(nanos / k_nanoseconds_per_second);
    t->set_nanos(static_cast<int32_t>(nanos % k_nanoseconds_per_second));
}

void UpdateTimeStamp(
    gsl::not_null<::google::longrunning::Operation*> const& op) {
    ::google::protobuf::Timestamp t;
    SetTimeStamp(&t, std::chrono::high_resolution_clock::now());
    op->mutable_metadata()->PackFrom(t);
}

[[nodiscard]] auto ToBazelActionResult(
    IExecutionResponse::ArtifactInfos const& artifacts,
    IExecutionResponse::DirSymlinks const& dir_symlinks,
    Storage const& storage) noexcept
    -> expected<bazel_re::ActionResult, std::string>;

[[nodiscard]] auto ToBazelAction(ArtifactDigest const& action_digest,
                                 Storage const& storage) noexcept
    -> expected<::bazel_re::Action, std::string>;

[[nodiscard]] auto ToBazelCommand(bazel_re::Action const& action,
                                  Storage const& storage) noexcept
    -> expected<bazel_re::Command, std::string>;
}  // namespace

auto ExecutionServiceImpl::ToIExecutionAction(
    ::bazel_re::Action const& action,
    ::bazel_re::Command const& command) const noexcept
    -> std::optional<IExecutionAction::Ptr> {
    auto const root_digest = ArtifactDigestFactory::FromBazel(
        storage_config_.hash_function.GetType(), action.input_root_digest());
    if (not root_digest) {
        return std::nullopt;
    }
    std::vector<std::string> const args(command.arguments().begin(),
                                        command.arguments().end());
    std::vector<std::string> const files(command.output_files().begin(),
                                         command.output_files().end());
    std::vector<std::string> const dirs(command.output_directories().begin(),
                                        command.output_directories().end());
    std::map<std::string, std::string> env_vars;
    for (auto const& x : command.environment_variables()) {
        env_vars.insert_or_assign(x.name(), x.value());
    }
    auto execution_action = api_.CreateAction(*root_digest,
                                              args,
                                              command.working_directory(),
                                              files,
                                              dirs,
                                              env_vars,
                                              /*properties=*/{});
    if (execution_action == nullptr) {
        return std::nullopt;
    }

    execution_action->SetCacheFlag(
        action.do_not_cache() ? IExecutionAction::CacheFlag::DoNotCacheOutput
                              : IExecutionAction::CacheFlag::CacheOutput);
    return execution_action;
}

auto ExecutionServiceImpl::ToBazelExecuteResponse(
    IExecutionResponse::Ptr const& i_execution_response) const noexcept
    -> expected<::bazel_re::ExecuteResponse, std::string> {
    auto artifacts = i_execution_response->Artifacts();
    if (not artifacts) {
        return unexpected{std::move(artifacts).error()};
    }
    auto dir_symlinks = i_execution_response->DirectorySymlinks();
    if (not dir_symlinks) {
        return unexpected{std::move(dir_symlinks).error()};
    }
    auto result = ToBazelActionResult(*std::move(artifacts).value(),
                                      *std::move(dir_symlinks).value(),
                                      storage_);
    if (not result) {
        return unexpected{std::move(result).error()};
    }

    auto action_result = *std::move(result);

    action_result.set_exit_code(i_execution_response->ExitCode());
    if (i_execution_response->HasStdErr()) {
        auto const cas_digest =
            storage_.CAS().StoreBlob(i_execution_response->StdErr(),
                                     /*is_executable=*/false);
        if (not cas_digest) {
            return unexpected{
                fmt::format("Could not store stderr of action {}",
                            i_execution_response->ActionDigest())};
        }
        (*action_result.mutable_stderr_digest()) =
            ArtifactDigestFactory::ToBazel(*cas_digest);
    }

    if (i_execution_response->HasStdOut()) {
        auto const cas_digest =
            storage_.CAS().StoreBlob(i_execution_response->StdOut(),
                                     /*is_executable=*/false);
        if (not cas_digest) {
            return unexpected{
                fmt::format("Could not store stdout of action {}",
                            i_execution_response->ActionDigest())};
        }
        (*action_result.mutable_stdout_digest()) =
            ArtifactDigestFactory::ToBazel(*cas_digest);
    }

    ::bazel_re::ExecuteResponse bazel_response{};
    (*bazel_response.mutable_result()) = std::move(action_result);
    bazel_response.set_cached_result(i_execution_response->IsCached());

    // we run the action locally, so no communication issues should happen
    bazel_response.mutable_status()->set_code(grpc::StatusCode::OK);
    return bazel_response;
}

void ExecutionServiceImpl::WriteResponse(
    ::bazel_re::ExecuteResponse const& execute_response,
    ::grpc::ServerWriter<::google::longrunning::Operation>* writer,
    ::google::longrunning::Operation&& op) noexcept {
    // send response to the client
    op.mutable_response()->PackFrom(execute_response);
    op.set_done(true);
    UpdateTimeStamp(&op);

    op_cache_.Set(op.name(), op);
    writer->Write(op);
}

auto ExecutionServiceImpl::Execute(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::ExecuteRequest* request,
    ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
    -> ::grpc::Status {
    auto const action_digest = ArtifactDigestFactory::FromBazel(
        storage_config_.hash_function.GetType(), request->action_digest());
    if (not action_digest) {
        logger_.Emit(LogLevel::Error, "{}", action_digest.error());
        return grpc::Status{grpc::StatusCode::INTERNAL, action_digest.error()};
    }

    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto kStr = "Could not acquire SharedLock";
        logger_.Emit(LogLevel::Error, "{}", kStr);
        return grpc::Status{grpc::StatusCode::INTERNAL, kStr};
    }

    auto action = ToBazelAction(*action_digest, storage_);
    if (not action) {
        logger_.Emit(LogLevel::Error, "{}", action.error());
        return ::grpc::Status{grpc::StatusCode::INTERNAL,
                              std::move(action).error()};
    }
    auto command = ToBazelCommand(*action, storage_);
    if (not command) {
        logger_.Emit(LogLevel::Error, "{}", command.error());
        return ::grpc::Status{grpc::StatusCode::INTERNAL,
                              std::move(command).error()};
    }
    auto i_execution_action = ToIExecutionAction(*action, *command);
    if (not i_execution_action) {
        auto const str = fmt::format("Could not create action from {}",
                                     action_digest->hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    logger_.Emit(LogLevel::Info, "Execute {}", action_digest->hash());
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
        action_digest->hash(),
        std::chrono::duration_cast<std::chrono::seconds>(t1 - t0).count());

    if (i_execution_response == nullptr) {
        auto const str =
            fmt::format("Failed to execute action {}", action_digest->hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    auto execute_response = ToBazelExecuteResponse(i_execution_response);
    if (not execute_response) {
        logger_.Emit(LogLevel::Error, "{}", execute_response.error());
        return ::grpc::Status{grpc::StatusCode::INTERNAL,
                              std::move(execute_response).error()};
    }
    ::bazel_re::ExecuteResponse response = *execute_response;
    SetTimeStamp(response.mutable_result()
                     ->mutable_execution_metadata()
                     ->mutable_worker_start_timestamp(),
                 t0);
    SetTimeStamp(response.mutable_result()
                     ->mutable_execution_metadata()
                     ->mutable_worker_completed_timestamp(),
                 t1);

    // Store the result in action cache
    if (i_execution_response->ExitCode() == 0 and not action->do_not_cache()) {
        if (not storage_.ActionCache().StoreResult(*action_digest,
                                                   response.result())) {
            auto const str =
                fmt::format("Could not store action result for action {}",
                            action_digest->hash());

            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
        }
    }

    WriteResponse(response, writer, std::move(op));
    return ::grpc::Status::OK;
}

auto ExecutionServiceImpl::WaitExecution(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::WaitExecutionRequest* request,
    ::grpc::ServerWriter<::google::longrunning::Operation>* writer)
    -> ::grpc::Status {
    auto const& hash = request->name();
    if (not IsHexString(hash)) {
        auto const str = fmt::format("Invalid hash {}", hash);
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, str};
    }
    logger_.Emit(LogLevel::Trace, "WaitExecution: {}", hash);
    auto op = op_cache_.Query(hash);
    while (op and not op->done()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        op = op_cache_.Query(hash);
    }
    if (not op) {
        auto const str = fmt::format(
            "Executing action {} not found in internal cache.", hash);
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    writer->Write(*op);
    logger_.Emit(LogLevel::Trace, "Finished WaitExecution {}", hash);
    return ::grpc::Status::OK;
}

namespace {
[[nodiscard]] auto ToBazelOutputDirectory(std::string path,
                                          ArtifactDigest const& digest,
                                          Storage const& storage) noexcept
    -> expected<bazel_re::OutputDirectory, std::string> {
    ::bazel_re::OutputDirectory out_dir{};
    *(out_dir.mutable_path()) = std::move(path);

    LocalCasReader reader(&storage.CAS());
    if (ProtocolTraits::IsNative(storage.GetHashFunction().GetType())) {
        // In native mode: set the digest directly.
        (*out_dir.mutable_tree_digest()) =
            ArtifactDigestFactory::ToBazel(digest);
    }
    else {
        // In compatible mode: Create a tree digest from directory
        // digest on the fly and set tree digest.
        auto const tree = reader.MakeTree(digest);
        if (not tree) {
            return unexpected{fmt::format("Failed to build bazel Tree for {}",
                                          digest.hash())};
        }

        auto const cas_digest =
            storage.CAS().StoreBlob(tree->SerializeAsString(),
                                    /*is_executable=*/false);
        if (not cas_digest) {
            return unexpected{fmt::format(
                "Failed to add to the storage the bazel Tree for {}",
                digest.hash())};
        }
        (*out_dir.mutable_tree_digest()) =
            ArtifactDigestFactory::ToBazel(*cas_digest);
    }
    return out_dir;
}

[[nodiscard]] auto ToBazelOutputSymlink(std::string path,
                                        ArtifactDigest const& digest,
                                        Storage const& storage) noexcept
    -> expected<bazel_re::OutputSymlink, std::string> {
    ::bazel_re::OutputSymlink out_link{};
    *(out_link.mutable_path()) = std::move(path);
    // recover the target of the symlink
    auto const cas_path =
        storage.CAS().BlobPath(digest, /*is_executable=*/false);
    if (not cas_path) {
        return unexpected{
            fmt::format("Failed to recover the symlink for {}", digest.hash())};
    }

    auto content = FileSystemManager::ReadFile(*cas_path);
    if (not content) {
        return unexpected{fmt::format(
            "Failed to read the symlink content for {}", digest.hash())};
    }

    *(out_link.mutable_target()) = *std::move(content);
    return out_link;
}

[[nodiscard]] auto ToBazelOutputFile(std::string path,
                                     Artifact::ObjectInfo const& info) noexcept
    -> ::bazel_re::OutputFile {
    ::bazel_re::OutputFile out_file{};
    (*out_file.mutable_path()) = std::move(path);
    (*out_file.mutable_digest()) = ArtifactDigestFactory::ToBazel(info.digest);
    out_file.set_is_executable(IsExecutableObject(info.type));
    return out_file;
}

[[nodiscard]] auto ToBazelActionResult(
    IExecutionResponse::ArtifactInfos const& artifacts,
    IExecutionResponse::DirSymlinks const& dir_symlinks,
    Storage const& storage) noexcept
    -> expected<bazel_re::ActionResult, std::string> {
    bazel_re::ActionResult result{};
    auto& result_files = *result.mutable_output_files();
    auto& result_file_links = *result.mutable_output_file_symlinks();
    auto& result_dirs = *result.mutable_output_directories();
    auto& result_dir_links = *result.mutable_output_directory_symlinks();

    auto const size = static_cast<int>(artifacts.size());
    result_files.Reserve(size);
    result_file_links.Reserve(size);
    result_dirs.Reserve(size);
    result_dir_links.Reserve(size);

    for (auto const& [path, info] : artifacts) {
        if (info.type == ObjectType::Tree) {
            auto out_dir = ToBazelOutputDirectory(path, info.digest, storage);
            if (not out_dir) {
                return unexpected{std::move(out_dir).error()};
            }
            result_dirs.Add(*std::move(out_dir));
        }
        else if (info.type == ObjectType::Symlink) {
            auto out_link = ToBazelOutputSymlink(path, info.digest, storage);
            if (not out_link) {
                return unexpected{std::move(out_link).error()};
            }
            if (dir_symlinks.contains(path)) {
                // directory symlink
                result_dir_links.Add(*std::move(out_link));
            }
            else {
                // file symlinks
                result_file_links.Add(*std::move(out_link));
            }
        }
        else {
            result_files.Add(ToBazelOutputFile(path, info));
        }
    }
    return result;
}

[[nodiscard]] auto ToBazelAction(ArtifactDigest const& action_digest,
                                 Storage const& storage) noexcept
    -> expected<::bazel_re::Action, std::string> {
    auto const action_path = storage.CAS().BlobPath(action_digest, false);
    if (not action_path) {
        return unexpected{fmt::format("could not retrieve blob {} from cas",
                                      action_digest.hash())};
    }

    ::bazel_re::Action action{};
    if (std::ifstream f(*action_path); not action.ParseFromIstream(&f)) {
        return unexpected{fmt::format("failed to parse action from blob {}",
                                      action_digest.hash())};
    }

    auto const hash_type = storage.GetHashFunction().GetType();
    auto const input_root_digest =
        ArtifactDigestFactory::FromBazel(hash_type, action.input_root_digest());
    if (not input_root_digest) {
        return unexpected{input_root_digest.error()};
    }
    auto const input_root_path =
        ProtocolTraits::IsNative(hash_type)
            ? storage.CAS().TreePath(*input_root_digest)
            : storage.CAS().BlobPath(*input_root_digest,
                                     /*is_executable=*/false);

    if (not input_root_path) {
        return unexpected{
            fmt::format("could not retrieve input root {} from cas",
                        input_root_digest->hash())};
    }
    return action;
}

[[nodiscard]] auto ToBazelCommand(bazel_re::Action const& action,
                                  Storage const& storage) noexcept
    -> expected<bazel_re::Command, std::string> {
    auto const command_digest = ArtifactDigestFactory::FromBazel(
        storage.GetHashFunction().GetType(), action.command_digest());
    if (not command_digest) {
        return unexpected{command_digest.error()};
    }
    auto const path =
        storage.CAS().BlobPath(*command_digest, /*is_executable=*/false);
    if (not path) {
        return unexpected{fmt::format("Could not retrieve blob {} from cas",
                                      command_digest->hash())};
    }

    ::bazel_re::Command c{};
    if (std::ifstream f(*path); not c.ParseFromIstream(&f)) {
        return unexpected{fmt::format("Failed to parse command from blob {}",
                                      command_digest->hash())};
    }
    return c;
}
}  // namespace
