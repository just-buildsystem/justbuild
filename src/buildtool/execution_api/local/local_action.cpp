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

#include "src/buildtool/execution_api/local/local_action.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "google/protobuf/repeated_ptr_field.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/common/ids.hpp"
#include "src/buildtool/execution_api/common/tree_reader.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/local_cas_reader.hpp"
#include "src/buildtool/execution_api/local/local_response.hpp"
#include "src/buildtool/execution_api/utils/outputscheck.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/system/system_command.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/path.hpp"

namespace {

/// \brief Removes specified directory if KeepBuildDir() is not set.
class BuildCleanupAnchor {
  public:
    explicit BuildCleanupAnchor(std::filesystem::path build_path) noexcept
        : build_path_{std::move(build_path)} {}
    BuildCleanupAnchor(BuildCleanupAnchor const&) = delete;
    BuildCleanupAnchor(BuildCleanupAnchor&&) = delete;
    auto operator=(BuildCleanupAnchor const&) -> BuildCleanupAnchor& = delete;
    auto operator=(BuildCleanupAnchor&&) -> BuildCleanupAnchor& = delete;
    ~BuildCleanupAnchor() {
        if (not FileSystemManager::RemoveDirectory(build_path_, true)) {
            Logger::Log(LogLevel::Error,
                        "Could not cleanup build directory {}",
                        build_path_.string());
        }
    }

  private:
    std::filesystem::path const build_path_;
};

[[nodiscard]] auto CreateDigestFromLocalOwnedTree(
    Storage const& storage,
    std::filesystem::path const& dir_path) -> std::optional<ArtifactDigest> {
    auto const& cas = storage.CAS();
    auto store_blob = [&cas](std::filesystem::path const& path,
                             auto is_exec) -> std::optional<ArtifactDigest> {
        return cas.StoreBlob</*kOwner=*/true>(path, is_exec);
    };
    auto store_tree =
        [&cas](std::string const& content) -> std::optional<ArtifactDigest> {
        return cas.StoreTree(content);
    };
    auto store_symlink =
        [&cas](std::string const& content) -> std::optional<ArtifactDigest> {
        return cas.StoreBlob(content);
    };
    return ProtocolTraits::IsNative(storage.GetHashFunction().GetType())
               ? BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
                     dir_path, store_blob, store_tree, store_symlink)
               : BazelMsgFactory::CreateDirectoryDigestFromLocalTree(
                     dir_path, store_blob, store_tree, store_symlink);
}

}  // namespace

auto LocalAction::Execute(Logger const* logger) noexcept
    -> IExecutionResponse::Ptr {

    auto do_cache = CacheEnabled(cache_flag_);
    auto const action = CreateActionDigest(root_digest_, not do_cache);
    if (not action) {
        if (logger != nullptr) {
            logger->Emit(LogLevel::Error,
                         "failed to create an action digest for {}",
                         root_digest_.hash());
        }
        return nullptr;
    }

    if (logger != nullptr) {
        logger->Emit(LogLevel::Trace,
                     "start execution\n"
                     " - exec_dir digest: {}\n"
                     " - action digest: {}",
                     root_digest_.hash(),
                     action->hash());
    }

    auto create_response = [](Logger const* logger,
                              std::string const& action_hash,
                              auto&&... args) -> IExecutionResponse::Ptr {
        try {
            return IExecutionResponse::Ptr{new LocalResponse{
                action_hash, std::forward<decltype(args)>(args)...}};
        } catch (...) {
            if (logger != nullptr) {
                logger->Emit(LogLevel::Error,
                             "failed to create a response for {}",
                             action_hash);
            }
        }
        return nullptr;
    };

    if (do_cache) {
        if (auto result =
                local_context_.storage->ActionCache().CachedResult(*action)) {
            if (result->exit_code() == 0 and
                ActionResultContainsExpectedOutputs(
                    *result, output_files_, output_dirs_)) {
                return create_response(
                    logger,
                    action->hash(),
                    LocalAction::Output{*std::move(result), /*is_cached=*/true},
                    local_context_.storage);
            }
        }
    }

    if (ExecutionEnabled(cache_flag_)) {
        if (auto output = Run(*action)) {
            if (cache_flag_ == CacheFlag::PretendCached) {
                // ensure the same id is created as if caching were enabled
                auto const action_cached =
                    CreateActionDigest(root_digest_, false);
                if (not action_cached) {
                    if (logger != nullptr) {
                        logger->Emit(
                            LogLevel::Error,
                            "failed to create a cached action digest for {}",
                            root_digest_.hash());
                    }
                    return nullptr;
                }

                output->is_cached = true;
                return create_response(logger,
                                       action_cached->hash(),
                                       *std::move(output),
                                       local_context_.storage);
            }
            return create_response(logger,
                                   action->hash(),
                                   *std::move(output),
                                   local_context_.storage);
        }
    }

    return nullptr;
}

auto LocalAction::Run(ArtifactDigest const& action_id) const noexcept
    -> std::optional<Output> {
    auto const exec_path = CreateUniquePath(
        local_context_.storage_config->ExecutionRoot() / action_id.hash());

    if (not exec_path) {
        return std::nullopt;
    }

    // anchor for cleaning up build directory at end of function (using RAII)
    auto anchor = BuildCleanupAnchor(*exec_path);

    auto const build_root = *exec_path / "build_root";
    if (not CreateDirectoryStructure(build_root)) {
        return std::nullopt;
    }

    if (cmdline_.empty()) {
        logger_.Emit(LogLevel::Error, "malformed command line");
        return std::nullopt;
    }

    // prepare actual command by including the launcher
    auto cmdline = local_context_.exec_config->launcher;
    std::copy(cmdline_.begin(), cmdline_.end(), std::back_inserter(cmdline));

    SystemCommand system{"LocalExecution"};
    auto const exit_code =
        system.Execute(cmdline, env_vars_, build_root / cwd_, *exec_path);
    if (exit_code.has_value()) {
        Output result{};
        result.action.set_exit_code(*exit_code);
        if (auto const digest = DigestFromOwnedFile(*exec_path / "stdout")) {
            *result.action.mutable_stdout_digest() =
                ArtifactDigestFactory::ToBazel(*digest);
        }
        if (auto const digest = DigestFromOwnedFile(*exec_path / "stderr")) {
            *result.action.mutable_stderr_digest() =
                ArtifactDigestFactory::ToBazel(*digest);
        }

        if (CollectAndStoreOutputs(&result.action, build_root / cwd_)) {
            if (cache_flag_ == CacheFlag::CacheOutput) {
                if (not local_context_.storage->ActionCache().StoreResult(
                        action_id, result.action)) {
                    logger_.Emit(LogLevel::Warning,
                                 "failed to store action results");
                }
            }
        }
        return result;
    }

    logger_.Emit(LogLevel::Error, "failed to execute commands");

    return std::nullopt;
}

auto LocalAction::StageInput(
    std::filesystem::path const& target_path,
    Artifact::ObjectInfo const& info,
    gsl::not_null<LocalAction::FileCopies*> copies) const noexcept -> bool {
    static std::string const kCopyFileName{"blob"};

    if (IsTreeObject(info.type)) {
        return FileSystemManager::CreateDirectory(target_path);
    }

    std::optional<std::filesystem::path> blob_path{};

    if (auto lookup = copies->find(info); lookup != copies->end()) {
        blob_path = lookup->second->GetPath() / kCopyFileName;
    }
    else {
        blob_path = local_context_.storage->CAS().BlobPath(
            info.digest, IsExecutableObject(info.type));
    }

    if (not blob_path) {
        logger_.Emit(LogLevel::Error,
                     "artifact with id {} is missing in CAS",
                     info.digest.hash());
        return false;
    }

    if (info.type == ObjectType::Symlink) {
        auto to =
            FileSystemManager::ReadContentAtPath(*blob_path, ObjectType::File);
        if (not to) {
            logger_.Emit(LogLevel::Error,
                         "could not read content of symlink {}",
                         (*blob_path).string());
            return false;
        }
        return FileSystemManager::CreateSymlink(*to, target_path);
    }

    if (not FileSystemManager::CreateDirectory(target_path.parent_path())) {
        return false;
    }
    auto res = FileSystemManager::CreateFileHardlink(
        *blob_path, target_path, LogLevel::Debug);
    if (res) {
        return true;
    }
    if (res.error() != std::errc::too_many_links) {
        logger_.Emit(LogLevel::Warning,
                     "Failed to link {} to {}: {}, {}",
                     nlohmann::json(blob_path->string()).dump(),
                     nlohmann::json(target_path.string()).dump(),
                     res.error().value(),
                     res.error().message());
        return false;
    }
    TmpDirPtr new_copy_dir =
        local_context_.storage_config->CreateTypedTmpDir("blob-copy");
    if (new_copy_dir == nullptr) {
        logger_.Emit(LogLevel::Warning,
                     "Failed to create a temporary directory for a blob copy");
        return false;
    }
    auto new_copy = new_copy_dir->GetPath() / kCopyFileName;
    if (not FileSystemManager::CopyFile(
            *blob_path, new_copy, IsExecutableObject(info.type))) {
        logger_.Emit(LogLevel::Warning,
                     "Failed to create a copy of {}",
                     info.ToString());
        return false;
    }
    if (not FileSystemManager::CreateFileHardlinkAs<true>(
            new_copy, target_path, info.type)) {
        return false;
    }
    try {
        copies->insert_or_assign(info, new_copy_dir);
    } catch (std::exception const& e) {
        logger_.Emit(LogLevel::Warning,
                     "Failed to update temp-copies map (continuing anyway): {}",
                     e.what());
    }
    return true;
}

auto LocalAction::StageInputs(
    std::filesystem::path const& exec_path,
    gsl::not_null<LocalAction::FileCopies*> copies) const noexcept -> bool {
    if (FileSystemManager::IsRelativePath(exec_path)) {
        return false;
    }
    auto reader = TreeReader<LocalCasReader>{&local_context_.storage->CAS()};
    auto result = reader.RecursivelyReadTreeLeafs(
        root_digest_, exec_path, /*include_trees=*/true);
    if (not result) {
        return false;
    }
    for (std::size_t i{}; i < result->paths.size(); ++i) {
        if (not StageInput(result->paths.at(i), result->infos.at(i), copies)) {
            return false;
        }
    }
    return true;
}

auto LocalAction::CreateDirectoryStructure(
    std::filesystem::path const& exec_path) const noexcept -> bool {
    // clean execution directory
    if (not FileSystemManager::RemoveDirectory(exec_path, true)) {
        logger_.Emit(LogLevel::Error, "failed to clean exec_path");
        return false;
    }

    // create process-exclusive execution directory
    if (not FileSystemManager::CreateDirectoryExclusive(exec_path)) {
        logger_.Emit(LogLevel::Error, "failed to exclusively create exec_path");
        return false;
    }

    // stage inputs (files, leaf trees) to execution directory
    {
        LocalAction::FileCopies copies{};
        if (not StageInputs(exec_path, &copies)) {
            logger_.Emit(LogLevel::Error,
                         "failed to stage input files to exec_path");
            return false;
        }
    }

    // create output paths
    auto const create_dir = [this](auto const& dir) {
        if (not FileSystemManager::CreateDirectory(dir)) {
            logger_.Emit(LogLevel::Error, "failed to create output directory");
            return false;
        }
        return true;
    };
    return std::all_of(output_files_.begin(),
                       output_files_.end(),
                       [this, &exec_path, &create_dir](auto const& local_path) {
                           auto dir =
                               (exec_path / cwd_ / local_path).parent_path();
                           return create_dir(dir);
                       }) and
           std::all_of(output_dirs_.begin(),
                       output_dirs_.end(),
                       [this, &exec_path, &create_dir](auto const& local_path) {
                           return create_dir(exec_path / cwd_ / local_path);
                       });
}

/// \brief We expect either a regular file, or a symlink.
auto LocalAction::CollectOutputFileOrSymlink(
    std::filesystem::path const& exec_path,
    std::string const& local_path) const noexcept
    -> std::optional<OutputFileOrSymlink> {
    auto file_path = exec_path / local_path;
    auto type = FileSystemManager::Type(file_path, /*allow_upwards=*/true);
    if (not type) {
        Logger::Log(LogLevel::Error, "expected known type at {}", local_path);
        return std::nullopt;
    }
    if (IsSymlinkObject(*type)) {
        if (auto content = FileSystemManager::ReadSymlink(file_path)) {
            // in native mode: check validity of symlink
            if (ProtocolTraits::IsNative(
                    local_context_.storage->GetHashFunction().GetType()) and
                not PathIsNonUpwards(*content)) {
                Logger::Log(
                    LogLevel::Error, "found invalid symlink at {}", local_path);
                return std::nullopt;
            }
            if (local_context_.storage->CAS().StoreBlob(*content)) {
                auto out_symlink = bazel_re::OutputSymlink{};
                out_symlink.set_path(local_path);
                out_symlink.set_target(*content);
                return out_symlink;
            }
        }
    }
    else if (IsFileObject(*type)) {
        bool is_executable = IsExecutableObject(*type);
        auto digest = local_context_.storage->CAS().StoreBlob</*kOwner=*/true>(
            file_path, is_executable);
        if (digest) {
            auto out_file = bazel_re::OutputFile{};
            out_file.set_path(local_path);
            *out_file.mutable_digest() =
                ArtifactDigestFactory::ToBazel(*digest);
            out_file.set_is_executable(is_executable);
            return out_file;
        }
    }
    else {
        Logger::Log(
            LogLevel::Error, "expected file or symlink at {}", local_path);
    }
    return std::nullopt;
}

auto LocalAction::CollectOutputDirOrSymlink(
    std::filesystem::path const& exec_path,
    std::string const& local_path) const noexcept
    -> std::optional<OutputDirOrSymlink> {
    auto dir_path = exec_path / local_path;
    auto type = FileSystemManager::Type(dir_path, /*allow_upwards=*/true);
    if (not type) {
        Logger::Log(LogLevel::Error, "expected known type at {}", local_path);
        return std::nullopt;
    }
    if (IsSymlinkObject(*type)) {
        if (auto content = FileSystemManager::ReadSymlink(dir_path)) {
            // in native mode: check validity of symlink
            if (ProtocolTraits::IsNative(
                    local_context_.storage->GetHashFunction().GetType()) and
                not PathIsNonUpwards(*content)) {
                Logger::Log(
                    LogLevel::Error, "found invalid symlink at {}", local_path);
                return std::nullopt;
            }
            if (local_context_.storage->CAS().StoreBlob(*content)) {
                auto out_symlink = bazel_re::OutputSymlink{};
                out_symlink.set_path(local_path);
                out_symlink.set_target(*content);
                return out_symlink;
            }
        }
    }
    else if (IsTreeObject(*type)) {
        if (auto digest = CreateDigestFromLocalOwnedTree(
                *local_context_.storage, dir_path)) {
            auto out_dir = bazel_re::OutputDirectory{};
            out_dir.set_path(local_path);
            (*out_dir.mutable_tree_digest()) =
                ArtifactDigestFactory::ToBazel(*digest);
            return out_dir;
        }
        Logger::Log(LogLevel::Error,
                    "found invalid entries in directory at {}",
                    local_path);
    }
    else {
        Logger::Log(
            LogLevel::Error, "expected directory or symlink at {}", local_path);
    }
    return std::nullopt;
}

auto LocalAction::CollectAndStoreOutputs(
    bazel_re::ActionResult* result,
    std::filesystem::path const& exec_path) const noexcept -> bool {
    try {
        logger_.Emit(LogLevel::Trace, "collecting outputs:");
        for (auto const& path : output_files_) {
            auto out = CollectOutputFileOrSymlink(exec_path, path);
            if (not out) {
                logger_.Emit(LogLevel::Error,
                             "could not collect output file or symlink {}",
                             path);
                return false;
            }
            if (std::holds_alternative<bazel_re::OutputSymlink>(*out)) {
                auto out_symlink = std::get<bazel_re::OutputSymlink>(*out);
                logger_.Emit(LogLevel::Trace,
                             " - symlink {}: {}",
                             path,
                             out_symlink.target());
                result->mutable_output_file_symlinks()->Add(
                    std::move(out_symlink));
            }
            else {
                auto out_file = std::get<bazel_re::OutputFile>(*out);
                auto const& digest = out_file.digest().hash();
                logger_.Emit(LogLevel::Trace, " - file {}: {}", path, digest);
                result->mutable_output_files()->Add(std::move(out_file));
            }
        }
        for (auto const& path : output_dirs_) {
            auto out = CollectOutputDirOrSymlink(exec_path, path);
            if (not out) {
                logger_.Emit(LogLevel::Error,
                             "could not collect output dir or symlink {}",
                             path);
                return false;
            }
            if (std::holds_alternative<bazel_re::OutputSymlink>(*out)) {
                auto out_symlink = std::get<bazel_re::OutputSymlink>(*out);
                logger_.Emit(LogLevel::Trace,
                             " - symlink {}: {}",
                             path,
                             out_symlink.target());
                result->mutable_output_directory_symlinks()->Add(
                    std::move(out_symlink));
            }
            else {
                auto out_dir = std::get<bazel_re::OutputDirectory>(*out);
                auto const& digest = out_dir.tree_digest().hash();
                logger_.Emit(LogLevel::Trace, " - dir {}: {}", path, digest);
                result->mutable_output_directories()->Add(std::move(out_dir));
            }
        }
        return true;
    } catch (std::exception const& ex) {
        // should never happen, but report nonetheless
        logger_.Emit(
            LogLevel::Error, "collecting outputs failed:\n{}", ex.what());
        return false;
    }
}

auto LocalAction::DigestFromOwnedFile(std::filesystem::path const& file_path)
    const noexcept -> std::optional<ArtifactDigest> {
    return local_context_.storage->CAS().StoreBlob</*kOwner=*/true>(
        file_path, /*is_executable=*/false);
}
