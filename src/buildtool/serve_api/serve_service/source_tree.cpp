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

#include "src/buildtool/serve_api/serve_service/source_tree.hpp"

#include <shared_mutex>
#include <thread>

#include "fmt/core.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/archive/archive_ops.hpp"

namespace {

auto ArchiveTypeToString(
    ::justbuild::just_serve::ServeArchiveTreeRequest_ArchiveType const& type)
    -> std::string {
    using ServeArchiveType =
        ::justbuild::just_serve::ServeArchiveTreeRequest_ArchiveType;
    switch (type) {
        case ServeArchiveType::ServeArchiveTreeRequest_ArchiveType_ZIP: {
            return "zip";
        }
        case ServeArchiveType::ServeArchiveTreeRequest_ArchiveType_TAR:
        default:
            return "archive";  //  default to .tar archive
    }
}

auto SymlinksResolveToPragmaSpecial(
    ::justbuild::just_serve::ServeArchiveTreeRequest_SymlinksResolve const&
        resolve) -> std::optional<PragmaSpecial> {
    using ServeSymlinksResolve =
        ::justbuild::just_serve::ServeArchiveTreeRequest_SymlinksResolve;
    switch (resolve) {
        case ServeSymlinksResolve::
            ServeArchiveTreeRequest_SymlinksResolve_IGNORE: {
            return PragmaSpecial::Ignore;
        }
        case ServeSymlinksResolve::
            ServeArchiveTreeRequest_SymlinksResolve_PARTIAL: {
            return PragmaSpecial::ResolvePartially;
        }
        case ServeSymlinksResolve::
            ServeArchiveTreeRequest_SymlinksResolve_COMPLETE: {
            return PragmaSpecial::ResolveCompletely;
        }
        case ServeSymlinksResolve::ServeArchiveTreeRequest_SymlinksResolve_NONE:
        default:
            return std::nullopt;  // default to NONE
    }
}

/// \brief Extracts the archive of given type into the destination directory
/// provided. Returns nullopt on success, or error string on failure.
[[nodiscard]] auto ExtractArchive(std::filesystem::path const& archive,
                                  std::string const& repo_type,
                                  std::filesystem::path const& dst_dir) noexcept
    -> std::optional<std::string> {
    if (repo_type == "archive") {
        return ArchiveOps::ExtractArchive(
            ArchiveType::TarAuto, archive, dst_dir);
    }
    if (repo_type == "zip") {
        return ArchiveOps::ExtractArchive(
            ArchiveType::ZipAuto, archive, dst_dir);
    }
    return "unrecognized archive type";
}

}  // namespace

auto SourceTreeService::GetSubtreeFromCommit(
    std::filesystem::path const& repo_path,
    std::string const& commit,
    std::string const& subdir,
    std::shared_ptr<Logger> const& logger) -> std::optional<std::string> {
    if (auto git_cas = GitCAS::Open(repo_path)) {
        if (auto repo = GitRepo::Open(git_cas)) {
            // wrap logger for GitRepo call
            auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                [logger, repo_path, commit](auto const& msg, bool fatal) {
                    if (fatal) {
                        auto err = fmt::format(
                            "ServeCommitTree: While retrieving subtree of "
                            "commit {} from repository {}:\n{}",
                            commit,
                            repo_path.string(),
                            msg);
                        logger->Emit(LogLevel::Trace, err);
                    }
                });
            if (auto tree_id = repo->GetSubtreeFromCommit(
                    commit, subdir, wrapped_logger)) {
                return tree_id;
            }
        }
    }
    return std::nullopt;
}

auto SourceTreeService::GetSubtreeFromTree(
    std::filesystem::path const& repo_path,
    std::string const& tree_id,
    std::string const& subdir,
    std::shared_ptr<Logger> const& logger) -> std::optional<std::string> {
    if (auto git_cas = GitCAS::Open(repo_path)) {
        if (auto repo = GitRepo::Open(git_cas)) {
            // wrap logger for GitRepo call
            auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                [logger, repo_path, tree_id](auto const& msg, bool fatal) {
                    if (fatal) {
                        auto err = fmt::format(
                            "ServeCommitTree: While retrieving subtree of tree "
                            "{} from repository {}:\n{}",
                            tree_id,
                            repo_path.string(),
                            msg);
                        logger->Emit(LogLevel::Trace, err);
                    }
                });
            if (auto subtree_id =
                    repo->GetSubtreeFromTree(tree_id, subdir, wrapped_logger)) {
                return subtree_id;
            }
        }
    }
    return std::nullopt;
}

auto SourceTreeService::GetBlobFromRepo(std::filesystem::path const& repo_path,
                                        std::string const& blob_id,
                                        std::shared_ptr<Logger> const& logger)
    -> std::optional<std::string> {
    if (auto git_cas = GitCAS::Open(repo_path)) {
        if (auto repo = GitRepo::Open(git_cas)) {
            // wrap logger for GitRepo call
            auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                [logger, repo_path, blob_id](auto const& msg, bool fatal) {
                    if (fatal) {
                        auto err = fmt::format(
                            "ServeCommitTree: While checking existence of blob "
                            "{} in repository {}:\n{}",
                            blob_id,
                            repo_path.string(),
                            msg);
                        logger->Emit(LogLevel::Trace, err);
                    }
                });
            auto res = repo->TryReadBlob(blob_id, wrapped_logger);
            if (not res.first) {
                return std::nullopt;
            }
            if (not res.second) {
                auto str = fmt::format("Blob {} not found in repository {}",
                                       blob_id,
                                       repo_path.string());
                logger->Emit(LogLevel::Trace, str);
            }
            return res.second;
        }
    }
    return std::nullopt;
}

auto SourceTreeService::ServeCommitTree(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::ServeCommitTreeRequest* request,
    ServeCommitTreeResponse* response) -> ::grpc::Status {
    auto const& commit{request->commit()};
    auto const& subdir{request->subdir()};
    // try in local build root Git cache
    if (auto tree_id = GetSubtreeFromCommit(
            StorageConfig::GitRoot(), commit, subdir, logger_)) {
        if (request->sync_tree()) {
            // sync tree with remote CAS
            auto digest = ArtifactDigest{*tree_id, 0, /*is_tree=*/true};
            auto repo = RepositoryConfig{};
            if (not repo.SetGitCAS(StorageConfig::GitRoot())) {
                auto str = fmt::format("Failed to SetGitCAS at {}",
                                       StorageConfig::GitRoot().string());
                logger_->Emit(LogLevel::Debug, str);
                response->set_status(ServeCommitTreeResponse::INTERNAL_ERROR);
                return ::grpc::Status::OK;
            }
            auto git_api = GitApi{&repo};
            if (not git_api.RetrieveToCas(
                    {Artifact::ObjectInfo{.digest = digest,
                                          .type = ObjectType::Tree}},
                    &(*remote_api_))) {
                auto str = fmt::format("Failed to sync tree {}", *tree_id);
                logger_->Emit(LogLevel::Debug, str);
                *(response->mutable_tree()) = std::move(*tree_id);
                response->set_status(ServeCommitTreeResponse::SYNC_ERROR);
                return ::grpc::Status::OK;
            }
        }
        // set response
        *(response->mutable_tree()) = std::move(*tree_id);
        response->set_status(ServeCommitTreeResponse::OK);
        return ::grpc::Status::OK;
    }
    // try given extra repositories, in order
    for (auto const& path : RemoteServeConfig::KnownRepositories()) {
        if (auto tree_id =
                GetSubtreeFromCommit(path, commit, subdir, logger_)) {
            if (request->sync_tree()) {
                // sync tree with remote CAS
                auto digest = ArtifactDigest{*tree_id, 0, /*is_tree=*/true};
                auto repo = RepositoryConfig{};
                if (not repo.SetGitCAS(path)) {
                    auto str =
                        fmt::format("Failed to SetGitCAS at {}", path.string());
                    logger_->Emit(LogLevel::Debug, str);
                    response->set_status(
                        ServeCommitTreeResponse::INTERNAL_ERROR);
                    return ::grpc::Status::OK;
                }
                auto git_api = GitApi{&repo};
                if (not git_api.RetrieveToCas(
                        {Artifact::ObjectInfo{.digest = digest,
                                              .type = ObjectType::Tree}},
                        &(*remote_api_))) {
                    auto str = fmt::format("Failed to sync tree {}", *tree_id);
                    logger_->Emit(LogLevel::Debug, str);
                    *(response->mutable_tree()) = std::move(*tree_id);
                    response->set_status(ServeCommitTreeResponse::SYNC_ERROR);
                }
            }
            // set response
            *(response->mutable_tree()) = std::move(*tree_id);
            response->set_status(ServeCommitTreeResponse::OK);
            return ::grpc::Status::OK;
        }
    }
    // commit not found
    response->set_status(ServeCommitTreeResponse::NOT_FOUND);
    return ::grpc::Status::OK;
}

auto SourceTreeService::SyncArchive(std::string const& tree_id,
                                    std::filesystem::path const& repo_path,
                                    bool sync_tree,
                                    ServeArchiveTreeResponse* response)
    -> ::grpc::Status {
    if (sync_tree) {
        // sync tree with remote CAS
        auto digest = ArtifactDigest{tree_id, 0, /*is_tree=*/true};
        auto repo = RepositoryConfig{};
        if (not repo.SetGitCAS(repo_path)) {
            auto str =
                fmt::format("Failed to SetGitCAS at {}", repo_path.string());
            logger_->Emit(LogLevel::Debug, str);
            response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        auto git_api = GitApi{&repo};
        if (not git_api.RetrieveToCas(
                {Artifact::ObjectInfo{.digest = digest,
                                      .type = ObjectType::Tree}},
                &(*remote_api_))) {
            auto str = fmt::format("Failed to sync tree {}", tree_id);
            logger_->Emit(LogLevel::Debug, str);
            *(response->mutable_tree()) = tree_id;
            response->set_status(ServeArchiveTreeResponse::SYNC_ERROR);
            return ::grpc::Status::OK;
        }
    }
    // set response
    *(response->mutable_tree()) = tree_id;
    response->set_status(ServeArchiveTreeResponse::OK);
    return ::grpc::Status::OK;
}

auto SourceTreeService::ResolveContentTree(
    std::string const& tree_id,
    std::filesystem::path const& repo_path,
    std::optional<PragmaSpecial> const& resolve_special,
    bool sync_tree,
    ServeArchiveTreeResponse* response) -> ::grpc::Status {
    if (resolve_special) {
        // get the resolved tree
        auto tree_id_file =
            StorageUtils::GetResolvedTreeIDFile(tree_id, *resolve_special);
        if (FileSystemManager::Exists(tree_id_file)) {
            // read resolved tree id
            auto resolved_tree_id = FileSystemManager::ReadFile(tree_id_file);
            if (not resolved_tree_id) {
                auto str =
                    fmt::format("Failed to read resolved tree id from file {}",
                                tree_id_file.string());
                logger_->Emit(LogLevel::Debug, str);
                response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
                return ::grpc::Status::OK;
            }
            return SyncArchive(
                *resolved_tree_id, repo_path, sync_tree, response);
        }
        // resolve tree
        ResolvedGitObject resolved_tree{};
        bool failed{false};
        {
            TaskSystem ts{std::max(1U, std::thread::hardware_concurrency())};
            resolve_symlinks_map_.ConsumeAfterKeysReady(
                &ts,
                {GitObjectToResolve{tree_id,
                                    ".",
                                    *resolve_special,
                                    /*known_info=*/std::nullopt}},
                [&resolved_tree](auto hashes) { resolved_tree = *hashes[0]; },
                [logger = logger_, tree_id, &failed](auto const& msg,
                                                     bool fatal) {
                    logger->Emit(LogLevel::Debug,
                                 "While resolving tree {}:\n{}",
                                 tree_id,
                                 msg);
                    failed = failed or fatal;
                });
        }
        if (failed) {
            auto str = fmt::format("Failed to resolve tree id {}", tree_id);
            logger_->Emit(LogLevel::Debug, str);
            response->set_status(ServeArchiveTreeResponse::RESOLVE_ERROR);
            return ::grpc::Status::OK;
        }
        // check for cycles
        auto error = DetectAndReportCycle(resolve_symlinks_map_, tree_id);
        if (error) {
            auto str = fmt::format(
                "Failed to resolve symlinks in tree {}:\n{}", tree_id, *error);
            logger_->Emit(LogLevel::Debug, str);
            response->set_status(ServeArchiveTreeResponse::RESOLVE_ERROR);
            return ::grpc::Status::OK;
        }
        // cache the resolved tree
        if (not StorageUtils::WriteTreeIDFile(tree_id_file, resolved_tree.id)) {
            auto str =
                fmt::format("Failed to write resolved tree id to file {}",
                            tree_id_file.string());
            logger_->Emit(LogLevel::Debug, str);
            response->set_status(ServeArchiveTreeResponse::RESOLVE_ERROR);
            return ::grpc::Status::OK;
        }
        return SyncArchive(resolved_tree.id, repo_path, sync_tree, response);
    }
    // if no special handling of symlinks, use given tree as-is
    return SyncArchive(tree_id, repo_path, sync_tree, response);
}

auto SourceTreeService::ImportToGit(
    std::filesystem::path const& unpack_path,
    std::filesystem::path const& archive_tree_id_file,
    std::string const& content,
    std::string const& archive_type,
    std::string const& subdir,
    std::optional<PragmaSpecial> const& resolve_special,
    bool sync_tree,
    ServeArchiveTreeResponse* response) -> ::grpc::Status {
    // do the initial commit; no need to guard, as the tmp location is unique
    auto git_repo = GitRepo::InitAndOpen(unpack_path,
                                         /*is_bare=*/false);
    if (not git_repo) {
        auto str = fmt::format("Could not initialize repository {}",
                               unpack_path.string());
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // wrap logger for GitRepo call
    auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger = logger_, unpack_path](auto const& msg, bool fatal) {
            if (fatal) {
                auto err = fmt::format(
                    "ServeArchiveTree: While staging and committing all in "
                    "repository {}:\n{}",
                    unpack_path.string(),
                    msg);
                logger->Emit(LogLevel::Trace, err);
            }
        });
    // stage and commit all
    // Important: message must be consistent with just-mr!
    auto mess = fmt::format("Content of {} {}", archive_type, content);
    auto commit_hash =
        git_repo->StageAndCommitAllAnonymous(mess, wrapped_logger);
    if (not commit_hash) {
        auto str =
            fmt::format("Failed to create initial commit in repository {}",
                        unpack_path.string());
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // create a tmp directory for the fetch to Git CAS
    auto tmp_dir = StorageUtils::CreateTypedTmpDir("import-to-git");
    if (not tmp_dir) {
        logger_->Emit(LogLevel::Debug,
                      "Failed to create tmp path for git import");
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // open the Git CAS repo
    auto just_git_cas = GitCAS::Open(StorageConfig::GitRoot());
    if (not just_git_cas) {
        auto str = fmt::format("Failed to open Git ODB at {}",
                               StorageConfig::GitRoot().string());
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto just_git_repo = GitRepo::Open(just_git_cas);
    if (not just_git_repo) {
        auto str = fmt::format("Failed to open Git repository {}",
                               StorageConfig::GitRoot().string());
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // wrap logger for GitRepo call
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger = logger_](auto const& msg, bool fatal) {
            if (fatal) {
                auto err = fmt::format(
                    "ServeArchiveTree: While fetching in repository {}:\n{}",
                    StorageConfig::GitRoot().string(),
                    msg);
                logger->Emit(LogLevel::Trace, err);
            }
        });
    // fetch the new commit into the Git CAS via tmp directory; the call is
    // thread-safe, so it needs no guarding
    if (not just_git_repo->LocalFetchViaTmpRepo(tmp_dir->GetPath(),
                                                unpack_path.string(),
                                                /*branch=*/std::nullopt,
                                                wrapped_logger)) {
        auto str =
            fmt::format("Failed to fetch commit {} into Git CAS", *commit_hash);
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // wrap logger for GitRepo call
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger = logger_, commit_hash](auto const& msg, bool fatal) {
            if (fatal) {
                auto err = fmt::format(
                    "ServeArchiveTree: While tagging commit {} in repository "
                    "{}:\n{}",
                    *commit_hash,
                    StorageConfig::GitRoot().string(),
                    msg);
                logger->Emit(LogLevel::Trace, err);
            }
        });
    // tag commit and keep it in Git CAS
    {
        // this is a non-thread-safe Git operation, so it must be guarded!
        std::shared_lock slock{mutex_};
        // open real repository at Git CAS location
        auto git_repo = GitRepo::Open(StorageConfig::GitRoot());
        if (not git_repo) {
            auto str = fmt::format("Failed to open Git CAS repository {}",
                                   StorageConfig::GitRoot().string());
            logger_->Emit(LogLevel::Debug, str);
            response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        // Important: message must be consistent with just-mr!
        if (not git_repo->KeepTag(*commit_hash,
                                  "Keep referenced tree alive",  // message
                                  wrapped_logger)) {
            auto str =
                fmt::format("Failed to tag and keep commit {}", *commit_hash);
            logger_->Emit(LogLevel::Debug, str);
            response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
    }
    // wrap logger for GitRepo call
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger = logger_, commit_hash](auto const& msg, bool fatal) {
            if (fatal) {
                auto err = fmt::format(
                    "ServeArchiveTree: While retrieving tree id of commit "
                    "{}:\n{}",
                    *commit_hash,
                    msg);
                logger->Emit(LogLevel::Trace, err);
            }
        });
    // get the root tree of this commit, to store in file; this is thread-safe
    auto tree_id =
        just_git_repo->GetSubtreeFromCommit(*commit_hash, ".", wrapped_logger);
    if (not tree_id) {
        auto str = fmt::format("Failed to retrieve tree id of commit {}",
                               *commit_hash);
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // write to tree id file
    if (not StorageUtils::WriteTreeIDFile(archive_tree_id_file, *tree_id)) {
        auto str = fmt::format("Failed to write tree id to file {}",
                               archive_tree_id_file.string());
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // wrap logger for GitRepo call
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger = logger_, subdir, tree_id](auto const& msg, bool fatal) {
            if (fatal) {
                auto err = fmt::format(
                    "ServeArchiveTree: While retrieving subtree {} of tree "
                    "{}:\n{}",
                    subdir,
                    *tree_id,
                    msg);
                logger->Emit(LogLevel::Trace, err);
            }
        });
    // get the subtree id; this is thread-safe
    auto subtree_id =
        just_git_repo->GetSubtreeFromTree(*tree_id, subdir, wrapped_logger);
    if (not subtree_id) {
        auto str = fmt::format("Failed to retrieve tree id of commit {}",
                               *commit_hash);
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    return ResolveContentTree(*subtree_id,
                              StorageConfig::GitRoot(),
                              resolve_special,
                              sync_tree,
                              response);
}

auto SourceTreeService::ServeArchiveTree(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::ServeArchiveTreeRequest* request,
    ServeArchiveTreeResponse* response) -> ::grpc::Status {
    auto const& content{request->content()};
    auto archive_type = ArchiveTypeToString(request->archive_type());
    auto const& subdir{request->subdir()};
    auto resolve_special =
        SymlinksResolveToPragmaSpecial(request->resolve_symlinks());

    // check for archive_tree_id_file
    auto archive_tree_id_file =
        StorageUtils::GetArchiveTreeIDFile(archive_type, content);
    if (FileSystemManager::Exists(archive_tree_id_file)) {
        // read archive_tree_id from file tree_id_file
        auto archive_tree_id =
            FileSystemManager::ReadFile(archive_tree_id_file);
        if (not archive_tree_id) {
            auto str = fmt::format("Failed to read tree id from file {}",
                                   archive_tree_id_file.string());
            logger_->Emit(LogLevel::Debug, str);
            response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        // check local build root Git cache
        if (auto subtree_id = GetSubtreeFromTree(
                StorageConfig::GitRoot(), *archive_tree_id, subdir, logger_)) {
            return ResolveContentTree(*subtree_id,
                                      StorageConfig::GitRoot(),
                                      resolve_special,
                                      request->sync_tree(),
                                      response);
        }
        // check known repositories
        for (auto const& path : RemoteServeConfig::KnownRepositories()) {
            if (auto subtree_id = GetSubtreeFromTree(
                    path, *archive_tree_id, subdir, logger_)) {
                return ResolveContentTree(*subtree_id,
                                          path,
                                          resolve_special,
                                          request->sync_tree(),
                                          response);
            }
        }
        // report error for missing tree specified in id file
        auto str =
            fmt::format("Tree {} is known, but missing!", *archive_tree_id);
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    std::optional<std::filesystem::path> content_cas_path{std::nullopt};
    // check if content blob is in Git cache
    if (auto data =
            GetBlobFromRepo(StorageConfig::GitRoot(), content, logger_)) {
        // add to CAS
        content_cas_path = StorageUtils::AddToCAS(*data);
    }
    if (not content_cas_path) {
        // check if content blob is in a known repository
        for (auto const& path : RemoteServeConfig::KnownRepositories()) {
            if (auto data = GetBlobFromRepo(path, content, logger_)) {
                // add to CAS
                content_cas_path = StorageUtils::AddToCAS(*data);
                if (content_cas_path) {
                    break;
                }
            }
        }
    }
    if (not content_cas_path) {
        // try to retrieve it from remote CAS
        auto digest = ArtifactDigest(content, 0, false);
        if (not(remote_api_->IsAvailable(digest) and
                remote_api_->RetrieveToCas(
                    {Artifact::ObjectInfo{.digest = digest,
                                          .type = ObjectType::File}},
                    &(*local_api_)))) {
            // content could not be found
            response->set_status(ServeArchiveTreeResponse::NOT_FOUND);
            return ::grpc::Status::OK;
        }
        // content should now be in CAS
        auto const& cas = Storage::Instance().CAS();
        content_cas_path =
            cas.BlobPath(digest, /*is_executable=*/false).value();
    }
    // extract archive
    auto tmp_dir = StorageUtils::CreateTypedTmpDir(archive_type);
    if (not tmp_dir) {
        auto str = fmt::format(
            "Failed to create tmp path for {} archive with content {}",
            archive_type,
            content);
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto res =
        ExtractArchive(*content_cas_path, archive_type, tmp_dir->GetPath());
    if (res != std::nullopt) {
        auto str = fmt::format("Failed to extract archive {} from CAS:\n{}",
                               content_cas_path->string(),
                               *res);
        logger_->Emit(LogLevel::Debug, str);
        response->set_status(ServeArchiveTreeResponse::UNPACK_ERROR);
        return ::grpc::Status::OK;
    }
    // import to git
    return ImportToGit(tmp_dir->GetPath(),
                       archive_tree_id_file,
                       content,
                       archive_type,
                       subdir,
                       resolve_special,
                       request->sync_tree(),
                       response);
}
