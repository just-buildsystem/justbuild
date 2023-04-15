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
#include <filesystem>

#include "gsl/gsl"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/local_response.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/system/system_command.hpp"

namespace {

/// \brief Removes specified directory if KeepBuildDir() is not set.
class BuildCleanupAnchor {
  public:
    explicit BuildCleanupAnchor(std::filesystem::path build_path) noexcept
        : build_path{std::move(build_path)} {}
    BuildCleanupAnchor(BuildCleanupAnchor const&) = delete;
    BuildCleanupAnchor(BuildCleanupAnchor&&) = delete;
    auto operator=(BuildCleanupAnchor const&) -> BuildCleanupAnchor& = delete;
    auto operator=(BuildCleanupAnchor&&) -> BuildCleanupAnchor& = delete;
    ~BuildCleanupAnchor() {
        if (not FileSystemManager::RemoveDirectory(build_path, true)) {
            Logger::Log(LogLevel::Error,
                        "Could not cleanup build directory {}",
                        build_path.string());
        }
    }

  private:
    std::filesystem::path const build_path{};
};

[[nodiscard]] auto CreateDigestFromLocalOwnedTree(
    gsl::not_null<Storage const*> const& storage,
    std::filesystem::path const& dir_path) -> std::optional<bazel_re::Digest> {
    auto const& cas = storage->CAS();
    auto store_blob = [&cas](auto path, auto is_exec) {
        return cas.StoreBlob</*kOwner=*/true>(path, is_exec);
    };
    auto store_tree = [&cas](auto bytes,
                             auto /*dir*/) -> std::optional<bazel_re::Digest> {
        return cas.StoreTree(bytes);
    };
    return Compatibility::IsCompatible()
               ? BazelMsgFactory::CreateDirectoryDigestFromLocalTree(
                     dir_path, store_blob, store_tree)
               : BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
                     dir_path, store_blob, store_tree);
}

}  // namespace

auto LocalAction::Execute(Logger const* logger) noexcept
    -> IExecutionResponse::Ptr {
    auto do_cache = CacheEnabled(cache_flag_);
    auto action = CreateActionDigest(root_digest_, not do_cache);

    if (logger != nullptr) {
        logger->Emit(LogLevel::Trace,
                     "start execution\n"
                     " - exec_dir digest: {}\n"
                     " - action digest: {}",
                     root_digest_.hash(),
                     NativeSupport::Unprefix(action.hash()));
    }

    if (do_cache) {
        if (auto result = storage_->ActionCache().CachedResult(action)) {
            if (result->exit_code() == 0) {
                return IExecutionResponse::Ptr{
                    new LocalResponse{action.hash(),
                                      {std::move(*result), /*is_cached=*/true},
                                      storage_}};
            }
        }
    }

    if (ExecutionEnabled(cache_flag_)) {
        if (auto output = Run(action)) {
            if (cache_flag_ == CacheFlag::PretendCached) {
                // ensure the same id is created as if caching were enabled
                auto action_id = CreateActionDigest(root_digest_, false).hash();
                output->is_cached = true;
                return IExecutionResponse::Ptr{new LocalResponse{
                    std::move(action_id), std::move(*output), storage_}};
            }
            return IExecutionResponse::Ptr{
                new LocalResponse{action.hash(), std::move(*output), storage_}};
        }
    }

    return nullptr;
}

auto LocalAction::Run(bazel_re::Digest const& action_id) const noexcept
    -> std::optional<Output> {
    auto exec_path =
        CreateUniquePath(StorageConfig::BuildRoot() / "exec_root" /
                         NativeSupport::Unprefix(action_id.hash()));

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

    auto cmdline = LocalExecutionConfig::GetLauncher();
    std::copy(cmdline_.begin(), cmdline_.end(), std::back_inserter(cmdline));

    SystemCommand system{"LocalExecution"};
    auto const command_output =
        system.Execute(cmdline, env_vars_, build_root, *exec_path);
    if (command_output.has_value()) {
        Output result{};
        result.action.set_exit_code(command_output->return_value);
        if (gsl::owner<bazel_re::Digest*> digest_ptr =
                DigestFromOwnedFile(command_output->stdout_file)) {
            result.action.set_allocated_stdout_digest(digest_ptr);
        }
        if (gsl::owner<bazel_re::Digest*> digest_ptr =
                DigestFromOwnedFile(command_output->stderr_file)) {
            result.action.set_allocated_stderr_digest(digest_ptr);
        }

        if (CollectAndStoreOutputs(&result.action, build_root)) {
            if (cache_flag_ == CacheFlag::CacheOutput) {
                if (not storage_->ActionCache().StoreResult(action_id,
                                                            result.action)) {
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

auto LocalAction::StageFile(std::filesystem::path const& target_path,
                            Artifact::ObjectInfo const& info) const -> bool {
    auto blob_path =
        storage_->CAS().BlobPath(info.digest, IsExecutableObject(info.type));

    if (not blob_path) {
        logger_.Emit(LogLevel::Error,
                     "artifact with id {} is missing in CAS",
                     info.digest.hash());
        return false;
    }

    return FileSystemManager::CreateDirectory(target_path.parent_path()) and
           FileSystemManager::CreateFileHardlink(*blob_path, target_path);
}

auto LocalAction::StageInputFiles(
    std::filesystem::path const& exec_path) const noexcept -> bool {
    if (FileSystemManager::IsRelativePath(exec_path)) {
        return false;
    }

    auto infos =
        storage_->CAS().RecursivelyReadTreeLeafs(root_digest_, exec_path);
    if (not infos) {
        return false;
    }
    for (std::size_t i{}; i < infos->first.size(); ++i) {
        if (not StageFile(infos->first.at(i), infos->second.at(i))) {
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

    // stage input files to execution directory
    if (not StageInputFiles(exec_path)) {
        logger_.Emit(LogLevel::Error,
                     "failed to stage input files to exec_path");
        return false;
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
                       [&exec_path, &create_dir](auto const& local_path) {
                           auto dir = (exec_path / local_path).parent_path();
                           return create_dir(dir);
                       }) and
           std::all_of(output_dirs_.begin(),
                       output_dirs_.end(),
                       [&exec_path, &create_dir](auto const& local_path) {
                           return create_dir(exec_path / local_path);
                       });
}

auto LocalAction::CollectOutputFile(std::filesystem::path const& exec_path,
                                    std::string const& local_path)
    const noexcept -> std::optional<bazel_re::OutputFile> {
    auto file_path = exec_path / local_path;
    auto type = FileSystemManager::Type(file_path);
    if ((not type) or (not IsFileObject(*type))) {
        Logger::Log(LogLevel::Error, "expected file at {}", local_path);
        return std::nullopt;
    }
    bool is_executable = IsExecutableObject(*type);
    auto digest =
        storage_->CAS().StoreBlob</*kOwner=*/true>(file_path, is_executable);
    if (digest) {
        auto out_file = bazel_re::OutputFile{};
        out_file.set_path(local_path);
        out_file.set_allocated_digest(
            gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{*digest}});
        out_file.set_is_executable(is_executable);
        return out_file;
    }
    return std::nullopt;
}

auto LocalAction::CollectOutputDir(std::filesystem::path const& exec_path,
                                   std::string const& local_path) const noexcept
    -> std::optional<bazel_re::OutputDirectory> {
    auto dir_path = exec_path / local_path;
    auto type = FileSystemManager::Type(dir_path);
    if ((not type) or (not IsTreeObject(*type))) {
        Logger::Log(LogLevel::Error, "expected directory at {}", local_path);
        return std::nullopt;
    }

    if (auto digest = CreateDigestFromLocalOwnedTree(storage_, dir_path)) {
        auto out_dir = bazel_re::OutputDirectory{};
        out_dir.set_path(local_path);
        out_dir.set_allocated_tree_digest(
            gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{*digest}});
        return out_dir;
    }
    return std::nullopt;
}

auto LocalAction::CollectAndStoreOutputs(
    bazel_re::ActionResult* result,
    std::filesystem::path const& exec_path) const noexcept -> bool {
    logger_.Emit(LogLevel::Trace, "collecting outputs:");
    for (auto const& path : output_files_) {
        auto out_file = CollectOutputFile(exec_path, path);
        if (not out_file) {
            logger_.Emit(
                LogLevel::Error, "could not collect output file {}", path);
            return false;
        }
        auto const& digest = out_file->digest().hash();
        logger_.Emit(LogLevel::Trace, " - file {}: {}", path, digest);
        result->mutable_output_files()->Add(std::move(*out_file));
    }
    for (auto const& path : output_dirs_) {
        auto out_dir = CollectOutputDir(exec_path, path);
        if (not out_dir) {
            logger_.Emit(
                LogLevel::Error, "could not collect output dir {}", path);
            return false;
        }
        auto const& digest = out_dir->tree_digest().hash();
        logger_.Emit(LogLevel::Trace, " - dir {}: {}", path, digest);
        result->mutable_output_directories()->Add(std::move(*out_dir));
    }

    return true;
}

auto LocalAction::DigestFromOwnedFile(std::filesystem::path const& file_path)
    const noexcept -> gsl::owner<bazel_re::Digest*> {
    if (auto digest = storage_->CAS().StoreBlob</*kOwner=*/true>(
            file_path, /*is_executable=*/false)) {
        return new bazel_re::Digest{std::move(*digest)};
    }
    return nullptr;
}
