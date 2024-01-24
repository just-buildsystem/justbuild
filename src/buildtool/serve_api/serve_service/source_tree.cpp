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
                        logger->Emit(LogLevel::Info, err);
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
                        logger->Emit(LogLevel::Info, err);
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
                        logger->Emit(LogLevel::Info, err);
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
                logger->Emit(LogLevel::Info, str);
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
                logger_->Emit(LogLevel::Error, str);
                response->set_status(ServeCommitTreeResponse::INTERNAL_ERROR);
                return ::grpc::Status::OK;
            }
            auto git_api = GitApi{&repo};
            if (not git_api.RetrieveToCas(
                    {Artifact::ObjectInfo{.digest = digest,
                                          .type = ObjectType::Tree}},
                    &(*remote_api_))) {
                auto str = fmt::format(
                    "Failed to sync tree {} from local Git cache", *tree_id);
                logger_->Emit(LogLevel::Error, str);
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
                    logger_->Emit(LogLevel::Error, str);
                    response->set_status(
                        ServeCommitTreeResponse::INTERNAL_ERROR);
                    return ::grpc::Status::OK;
                }
                auto git_api = GitApi{&repo};
                if (not git_api.RetrieveToCas(
                        {Artifact::ObjectInfo{.digest = digest,
                                              .type = ObjectType::Tree}},
                        &(*remote_api_))) {
                    auto str = fmt::format(
                        "Failed to sync tree {} from known repository {}",
                        *tree_id,
                        path.string());
                    logger_->Emit(LogLevel::Error, str);
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
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        auto git_api = GitApi{&repo};
        if (not git_api.RetrieveToCas(
                {Artifact::ObjectInfo{.digest = digest,
                                      .type = ObjectType::Tree}},
                &(*remote_api_))) {
            auto str = fmt::format("Failed to sync tree {} from repository {}",
                                   tree_id,
                                   repo_path.string());
            logger_->Emit(LogLevel::Error, str);
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
                logger_->Emit(LogLevel::Error, str);
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
                    logger->Emit(LogLevel::Error,
                                 "While resolving tree {}:\n{}",
                                 tree_id,
                                 msg);
                    failed = failed or fatal;
                });
        }
        if (failed) {
            auto str = fmt::format("Failed to resolve tree id {}", tree_id);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeArchiveTreeResponse::RESOLVE_ERROR);
            return ::grpc::Status::OK;
        }
        // check for cycles
        auto error = DetectAndReportCycle(resolve_symlinks_map_, tree_id);
        if (error) {
            auto str = fmt::format(
                "Failed to resolve symlinks in tree {}:\n{}", tree_id, *error);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeArchiveTreeResponse::RESOLVE_ERROR);
            return ::grpc::Status::OK;
        }
        // cache the resolved tree
        if (not StorageUtils::WriteTreeIDFile(tree_id_file, resolved_tree.id)) {
            auto str =
                fmt::format("Failed to write resolved tree id to file {}",
                            tree_id_file.string());
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeArchiveTreeResponse::RESOLVE_ERROR);
            return ::grpc::Status::OK;
        }
        return SyncArchive(resolved_tree.id, repo_path, sync_tree, response);
    }
    // if no special handling of symlinks, use given tree as-is
    return SyncArchive(tree_id, repo_path, sync_tree, response);
}

auto SourceTreeService::CommonImportToGit(
    std::filesystem::path const& root_path,
    std::string const& commit_message)
    -> std::variant<std::string, std::string> {
    using result_t = std::variant<std::string, std::string>;
    // do the initial commit; no need to guard, as the tmp location is unique
    auto git_repo = GitRepo::InitAndOpen(root_path,
                                         /*is_bare=*/false);
    if (not git_repo) {
        auto str = fmt::format("Could not initialize repository {}",
                               root_path.string());
        return result_t(std::in_place_index<0>, str);
    }
    // wrap logger for GitRepo call
    std::string err;
    auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [root_path, &err](auto const& msg, bool fatal) {
            if (fatal) {
                err = fmt::format(
                    "While staging and committing all in repository {}:\n{}",
                    root_path.string(),
                    msg);
            }
        });
    // stage and commit all
    auto commit_hash =
        git_repo->StageAndCommitAllAnonymous(commit_message, wrapped_logger);
    if (not commit_hash) {
        auto str =
            fmt::format("Failed to create initial commit in repository {}\n{}",
                        root_path.string(),
                        err);
        return result_t(std::in_place_index<0>, str);
    }
    // create a tmp directory for the fetch to Git CAS
    auto tmp_dir = StorageUtils::CreateTypedTmpDir("import-to-git");
    if (not tmp_dir) {
        return result_t(
            std::in_place_index<0>,
            std::string("Failed to create tmp path for git import"));
    }
    // open the Git CAS repo
    auto just_git_cas = GitCAS::Open(StorageConfig::GitRoot());
    if (not just_git_cas) {
        auto str = fmt::format("Failed to open Git ODB at {}",
                               StorageConfig::GitRoot().string());
        return result_t(std::in_place_index<0>, str);
    }
    auto just_git_repo = GitRepo::Open(just_git_cas);
    if (not just_git_repo) {
        auto str = fmt::format("Failed to open Git repository {}",
                               StorageConfig::GitRoot().string());
        return result_t(std::in_place_index<0>, str);
    }
    // wrap logger for GitRepo call
    err.clear();
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [&err](auto const& msg, bool fatal) {
            if (fatal) {
                err = fmt::format("While fetching in repository {}:\n{}",
                                  StorageConfig::GitRoot().string(),
                                  msg);
            }
        });
    // fetch the new commit into the Git CAS via tmp directory; the call is
    // thread-safe, so it needs no guarding
    if (not just_git_repo->LocalFetchViaTmpRepo(tmp_dir->GetPath(),
                                                root_path.string(),
                                                /*branch=*/std::nullopt,
                                                wrapped_logger)) {
        auto str = fmt::format(
            "Failed to fetch commit {} into Git CAS\n{}", *commit_hash, err);
        return result_t(std::in_place_index<0>, str);
    }
    // wrap logger for GitRepo call
    err.clear();
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [commit_hash, &err](auto const& msg, bool fatal) {
            if (fatal) {
                err =
                    fmt::format("While tagging commit {} in repository {}:\n{}",
                                *commit_hash,
                                StorageConfig::GitRoot().string(),
                                msg);
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
            return result_t(std::in_place_index<0>, str);
        }
        // Important: message must be consistent with just-mr!
        if (not git_repo->KeepTag(*commit_hash,
                                  "Keep referenced tree alive",  // message
                                  wrapped_logger)) {
            auto str = fmt::format(
                "Failed to tag and keep commit {}\n{}", *commit_hash, err);
            return result_t(std::in_place_index<0>, str);
        }
    }
    // wrap logger for GitRepo call
    err.clear();
    wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [commit_hash, &err](auto const& msg, bool fatal) {
            if (fatal) {
                err = fmt::format("While retrieving tree id of commit {}:\n{}",
                                  *commit_hash,
                                  msg);
            }
        });
    // get the root tree of this commit; this is thread-safe
    auto tree_id =
        just_git_repo->GetSubtreeFromCommit(*commit_hash, ".", wrapped_logger);
    if (not tree_id) {
        auto str = fmt::format(
            "Failed to retrieve tree id of commit {}\n{}", *commit_hash, err);
        return result_t(std::in_place_index<0>, str);
    }
    // return the root tree id
    return result_t(std::in_place_index<1>, *tree_id);
}

auto SourceTreeService::ArchiveImportToGit(
    std::filesystem::path const& unpack_path,
    std::filesystem::path const& archive_tree_id_file,
    std::string const& content,
    std::string const& archive_type,
    std::string const& subdir,
    std::optional<PragmaSpecial> const& resolve_special,
    bool sync_tree,
    ServeArchiveTreeResponse* response) -> ::grpc::Status {
    // Important: commit message must match that in just-mr!
    auto commit_message =
        fmt::format("Content of {} {}", archive_type, content);
    auto res = CommonImportToGit(unpack_path, commit_message);
    if (res.index() == 0) {
        // report the error
        logger_->Emit(LogLevel::Error, std::get<0>(res));
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto const& tree_id = std::get<1>(res);
    // write to tree id file
    if (not StorageUtils::WriteTreeIDFile(archive_tree_id_file, tree_id)) {
        auto str = fmt::format("Failed to write tree id to file {}",
                               archive_tree_id_file.string());
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // open the Git CAS repo
    auto just_git_cas = GitCAS::Open(StorageConfig::GitRoot());
    if (not just_git_cas) {
        auto str = fmt::format("Failed to open Git ODB at {}",
                               StorageConfig::GitRoot().string());
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto just_git_repo = GitRepo::Open(just_git_cas);
    if (not just_git_repo) {
        auto str = fmt::format("Failed to open Git repository {}",
                               StorageConfig::GitRoot().string());
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // wrap logger for GitRepo call
    auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger = logger_, subdir, tree_id](auto const& msg, bool fatal) {
            if (fatal) {
                auto err =
                    fmt::format("While retrieving subtree {} of tree {}:\n{}",
                                subdir,
                                tree_id,
                                msg);
                logger->Emit(LogLevel::Error, err);
            }
        });
    // get the subtree id; this is thread-safe
    auto subtree_id =
        just_git_repo->GetSubtreeFromTree(tree_id, subdir, wrapped_logger);
    if (not subtree_id) {
        auto str = fmt::format(
            "Failed to retrieve subtree {} of tree {}", subdir, tree_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    return ResolveContentTree(*subtree_id,
                              StorageConfig::GitRoot(),
                              resolve_special,
                              sync_tree,
                              response);
}

auto SourceTreeService::IsTreeInRepo(std::string const& tree_id,
                                     std::filesystem::path const& repo_path,
                                     std::shared_ptr<Logger> const& logger)
    -> bool {
    if (auto git_cas = GitCAS::Open(repo_path)) {
        if (auto repo = GitRepo::Open(git_cas)) {
            // wrap logger for GitRepo call
            auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
                [logger, repo_path, tree_id](auto const& msg, bool fatal) {
                    if (fatal) {
                        auto err = fmt::format(
                            "SourceTreeService: While checking existence of "
                            "tree {} in repository {}:\n{}",
                            tree_id,
                            repo_path.string(),
                            msg);
                        logger->Emit(LogLevel::Info, err);
                    }
                });
            if (auto res = repo->CheckTreeExists(tree_id, wrapped_logger)) {
                return *res;
            }
        }
    }
    return false;  // tree not found
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
            logger_->Emit(LogLevel::Error, str);
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
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // check if content is in local CAS already
    auto digest = ArtifactDigest(content, 0, false);
    auto const& cas = Storage::Instance().CAS();
    std::optional<std::filesystem::path> content_cas_path{std::nullopt};
    if (content_cas_path = cas.BlobPath(digest, /*is_executable=*/false);
        not content_cas_path) {
        // check if content blob is in Git cache
        if (auto data =
                GetBlobFromRepo(StorageConfig::GitRoot(), content, logger_)) {
            // add to CAS
            content_cas_path = StorageUtils::AddToCAS(*data);
        }
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
        content_cas_path = cas.BlobPath(digest, /*is_executable=*/false);
        if (not content_cas_path) {
            auto str = fmt::format(
                "Retrieving content {} from CAS failed unexpectedly", content);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
    }
    // extract archive
    auto tmp_dir = StorageUtils::CreateTypedTmpDir(archive_type);
    if (not tmp_dir) {
        auto str = fmt::format(
            "Failed to create tmp path for {} archive with content {}",
            archive_type,
            content);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeArchiveTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto res =
        ExtractArchive(*content_cas_path, archive_type, tmp_dir->GetPath());
    if (res != std::nullopt) {
        auto str = fmt::format("Failed to extract archive {} from CAS:\n{}",
                               content_cas_path->string(),
                               *res);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeArchiveTreeResponse::UNPACK_ERROR);
        return ::grpc::Status::OK;
    }
    // import to git
    return ArchiveImportToGit(tmp_dir->GetPath(),
                              archive_tree_id_file,
                              content,
                              archive_type,
                              subdir,
                              resolve_special,
                              request->sync_tree(),
                              response);
}

auto SourceTreeService::DistdirImportToGit(
    std::string const& distdir_tree_id,
    std::string const& content_id,
    std::unordered_map<std::string, std::string> const& content_list,
    bool sync_tree,
    ServeDistdirTreeResponse* response) -> ::grpc::Status {
    // create tmp directory for the distdir
    auto distdir_tmp_dir = StorageUtils::CreateTypedTmpDir("distdir");
    if (not distdir_tmp_dir) {
        auto str = fmt::format(
            "Failed to create tmp path for distdir target {}", content_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeDistdirTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto const& tmp_path = distdir_tmp_dir->GetPath();
    // link the CAS blobs into the tmp dir
    auto const& cas = Storage::Instance().CAS();
    if (not std::all_of(content_list.begin(),
                        content_list.end(),
                        [&cas, tmp_path](auto const& kv) {
                            auto content_path = cas.BlobPath(
                                ArtifactDigest(kv.second, 0, /*is_tree=*/false),
                                /*is_executable=*/false);
                            if (content_path) {
                                return FileSystemManager::CreateFileHardlink(
                                    *content_path,  // from: cas_path/content_id
                                    tmp_path / kv.first);  // to: tmp_path/name
                            }
                            return false;
                        })) {
        auto str =
            fmt::format("Failed to create links to CAS content {}", content_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeDistdirTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // Important: commit message must match that in just-mr!
    auto commit_message = fmt::format("Content of distdir {}", content_id);
    auto res = CommonImportToGit(tmp_path, commit_message);
    if (res.index() == 0) {
        // report the error
        logger_->Emit(LogLevel::Error, std::get<0>(res));
        response->set_status(ServeDistdirTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto const& tree_id = std::get<1>(res);
    // check the committed tree matches what we expect
    if (tree_id != distdir_tree_id) {
        // something is very wrong...
        auto str = fmt::format(
            "Unexpected mismatch for tree of committed distdir:\nexpected {} "
            "but got {}",
            distdir_tree_id,
            tree_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeDistdirTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // if asked, sync tree (and implicitly all blobs) with remote CAS
    if (sync_tree) {
        if (not local_api_->RetrieveToCas(
                {Artifact::ObjectInfo{
                    .digest =
                        ArtifactDigest{distdir_tree_id, 0, /*is_tree=*/true},
                    .type = ObjectType::Tree}},
                &(*remote_api_))) {
            auto str = fmt::format("Failed to sync tree {} from local CAS",
                                   distdir_tree_id);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeDistdirTreeResponse::SYNC_ERROR);
            return ::grpc::Status::OK;
        }
    }
    // set response on success
    response->set_tree(distdir_tree_id);
    response->set_status(ServeDistdirTreeResponse::OK);
    return ::grpc::Status::OK;
}

auto SourceTreeService::ServeDistdirTree(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::ServeDistdirTreeRequest* request,
    ServeDistdirTreeResponse* response) -> ::grpc::Status {
    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeDistdirTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // create in-memory tree from distfiles map
    GitRepo::tree_entries_t entries{};
    entries.reserve(request->distfiles().size());

    bool blob_found{};
    auto const& cas = Storage::Instance().CAS();
    std::unordered_map<std::string, std::string> content_list{};
    content_list.reserve(request->distfiles().size());

    for (auto const& kv : request->distfiles()) {
        auto const& content = kv.content();
        // check content blob is known
        auto digest = ArtifactDigest(content, 0, /*is_tree=*/false);
        // first check the local CAS itself
        if (blob_found = static_cast<bool>(
                cas.BlobPath(digest, /*is_executable=*/false));
            blob_found) {
        }
        else {
            // check local Git cache
            if (auto data = GetBlobFromRepo(
                    StorageConfig::GitRoot(), content, logger_)) {
                // add content to local CAS
                if (not cas.StoreBlob(*data, /*is_executable=*/false)) {
                    auto str = fmt::format(
                        "Failed to store content {} from local Git cache to "
                        "local CAS",
                        content);
                    logger_->Emit(LogLevel::Error, str);
                    response->set_status(
                        ServeDistdirTreeResponse::INTERNAL_ERROR);
                    return ::grpc::Status::OK;
                }
                blob_found = true;
            }
            else {
                // check known repositories
                for (auto const& path :
                     RemoteServeConfig::KnownRepositories()) {
                    if (auto data = GetBlobFromRepo(path, content, logger_)) {
                        // add content to local CAS
                        if (not cas.StoreBlob(*data, /*is_executable=*/false)) {
                            auto str = fmt::format(
                                "Failed to store content {} from known "
                                "repository {} to local CAS",
                                path.string(),
                                content);
                            logger_->Emit(LogLevel::Error, str);
                            response->set_status(
                                ServeDistdirTreeResponse::INTERNAL_ERROR);
                            return ::grpc::Status::OK;
                        }
                        blob_found = true;
                        break;
                    }
                }
                if (not blob_found) {
                    // Explanation: clang-tidy gets confused by the break in the
                    // for-loop above into falsely believing that it can reach
                    // this point with the variable "digest" already moved, so
                    // we work around this by creating a new variable here
                    auto digest_clone =
                        ArtifactDigest(content, 0, /*is_tree=*/false);
                    // check remote CAS
                    if (remote_api_->IsAvailable(digest_clone)) {
                        // retrieve content to local CAS
                        if (not remote_api_->RetrieveToCas(
                                {Artifact::ObjectInfo{
                                    .digest = digest_clone,
                                    .type = ObjectType::File}},
                                &(*local_api_))) {
                            auto str = fmt::format(
                                "Failed to retrieve content {} from remote to "
                                "local CAS",
                                content);
                            logger_->Emit(LogLevel::Error, str);
                            response->set_status(
                                ServeDistdirTreeResponse::INTERNAL_ERROR);
                            return ::grpc::Status::OK;
                        }
                        // Note: no need to put this digest in the list for
                        // later sync, as we know the remote already has it
                        blob_found = true;
                    }
                }
            }
        }
        // error out if blob is not known
        if (not blob_found) {
            auto str = fmt::format("Content {} is not known", content);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeDistdirTreeResponse::NOT_FOUND);
            return ::grpc::Status::OK;
        }
        // store content blob to the entries list, using the expected raw id
        if (auto raw_id = FromHexString(content)) {
            entries[*raw_id].emplace_back(kv.name(), ObjectType::File);
        }
        else {
            auto str = fmt::format(
                "Conversion of content {} to raw id failed unexpectedly",
                content);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeDistdirTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        // store to content_list for import-to-git hardlinking
        content_list.insert_or_assign(kv.name(), kv.content());
    }
    // get hash of distdir content; this must match with that in just-mr
    auto content_id =
        HashFunction::ComputeBlobHash(nlohmann::json(content_list).dump())
            .HexString();
    // create in-memory tree of the distdir, now that we know we have all blobs
    auto tree = GitRepo::CreateShallowTree(entries);
    if (not tree) {
        auto str =
            std::string{"Failed to construct in-memory tree for distdir"};
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeDistdirTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // get hash from raw_id
    auto tree_id = ToHexString(tree->first);
    // add tree to local CAS
    auto tree_digest = cas.StoreTree(tree->second);
    if (not tree_digest) {
        auto str = fmt::format("Failed to store distdir tree {} to local CAS",
                               tree_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeDistdirTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // check if tree is already in Git cache
    if (IsTreeInRepo(tree_id, StorageConfig::GitRoot(), logger_)) {
        // if asked, sync tree and all blobs with remote CAS
        if (request->sync_tree()) {
            if (not local_api_->RetrieveToCas(
                    {Artifact::ObjectInfo{
                        .digest = ArtifactDigest{tree_id, 0, /*is_tree=*/true},
                        .type = ObjectType::Tree}},
                    &(*remote_api_))) {
                auto str = fmt::format("Failed to sync tree {} from local CAS",
                                       tree_id);
                logger_->Emit(LogLevel::Error, str);
                response->set_status(ServeDistdirTreeResponse::SYNC_ERROR);
                return ::grpc::Status::OK;
            }
        }
        // set response on success
        response->set_tree(tree_id);
        response->set_status(ServeDistdirTreeResponse::OK);
        return ::grpc::Status::OK;
    }
    // check if tree is in a known repository
    for (auto const& path : RemoteServeConfig::KnownRepositories()) {
        if (IsTreeInRepo(tree_id, path, logger_)) {
            // if asked, sync tree and all blobs with remote CAS
            if (request->sync_tree()) {
                if (not local_api_->RetrieveToCas(
                        {Artifact::ObjectInfo{
                            .digest =
                                ArtifactDigest{tree_id, 0, /*is_tree=*/true},
                            .type = ObjectType::Tree}},
                        &(*remote_api_))) {
                    auto str = fmt::format(
                        "Failed to sync tree {} from local CAS", tree_id);
                    logger_->Emit(LogLevel::Error, str);
                    response->set_status(ServeDistdirTreeResponse::SYNC_ERROR);
                    return ::grpc::Status::OK;
                }
            }
            // set response on success
            response->set_tree(tree_id);
            response->set_status(ServeDistdirTreeResponse::OK);
            return ::grpc::Status::OK;
        }
    }
    // otherwise, we import the tree from CAS ourselves
    return DistdirImportToGit(
        tree_id, content_id, content_list, request->sync_tree(), response);
}

auto SourceTreeService::ServeContent(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::ServeContentRequest* request,
    ServeContentResponse* response) -> ::grpc::Status {
    auto const& content{request->content()};
    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeContentResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto const digest = ArtifactDigest{content, 0, /*is_tree=*/false};
    // check if content blob is in Git cache
    if (auto data =
            GetBlobFromRepo(StorageConfig::GitRoot(), content, logger_)) {
        // upload blob to remote CAS
        auto repo = RepositoryConfig{};
        if (not repo.SetGitCAS(StorageConfig::GitRoot())) {
            auto str = fmt::format("Failed to SetGitCAS at {}",
                                   StorageConfig::GitRoot().string());
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeContentResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        auto git_api = GitApi{&repo};
        if (not git_api.RetrieveToCas(
                {Artifact::ObjectInfo{.digest = digest,
                                      .type = ObjectType::File}},
                &(*remote_api_))) {
            auto str = fmt::format(
                "Failed to sync content {} from local Git cache", content);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeContentResponse::SYNC_ERROR);
            return ::grpc::Status::OK;
        }
        // success!
        response->set_status(ServeContentResponse::OK);
        return ::grpc::Status::OK;
    }
    // check if content blob is in a known repository
    for (auto const& path : RemoteServeConfig::KnownRepositories()) {
        if (auto data = GetBlobFromRepo(path, content, logger_)) {
            // upload blob to remote CAS
            auto repo = RepositoryConfig{};
            if (not repo.SetGitCAS(path)) {
                auto str =
                    fmt::format("Failed to SetGitCAS at {}", path.string());
                logger_->Emit(LogLevel::Error, str);
                response->set_status(ServeContentResponse::INTERNAL_ERROR);
                return ::grpc::Status::OK;
            }
            auto git_api = GitApi{&repo};
            if (not git_api.RetrieveToCas(
                    {Artifact::ObjectInfo{.digest = digest,
                                          .type = ObjectType::File}},
                    &(*remote_api_))) {
                auto str = fmt::format(
                    "Failed to sync content {} from known repository {}",
                    content,
                    path.string());
                logger_->Emit(LogLevel::Error, str);
                response->set_status(ServeContentResponse::SYNC_ERROR);
                return ::grpc::Status::OK;
            }
            // success!
            response->set_status(ServeContentResponse::OK);
            return ::grpc::Status::OK;
        }
    }
    // check also in the local CAS
    if (local_api_->IsAvailable(digest) and
        not local_api_->RetrieveToCas(
            {Artifact::ObjectInfo{.digest = digest, .type = ObjectType::File}},
            &(*remote_api_))) {
        auto str =
            fmt::format("Failed to sync content {} from local CAS", content);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeContentResponse::SYNC_ERROR);
        return ::grpc::Status::OK;
    }
    // content blob not known
    response->set_status(ServeContentResponse::NOT_FOUND);
    return ::grpc::Status::OK;
}

auto SourceTreeService::ServeTree(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::ServeTreeRequest* request,
    ServeTreeResponse* response) -> ::grpc::Status {
    auto const& tree_id{request->tree()};
    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto digest = ArtifactDigest{tree_id, 0, /*is_tree=*/true};
    // check if tree is in Git cache
    if (IsTreeInRepo(tree_id, StorageConfig::GitRoot(), logger_)) {
        // upload tree to remote CAS
        auto repo = RepositoryConfig{};
        if (not repo.SetGitCAS(StorageConfig::GitRoot())) {
            auto str = fmt::format("Failed to SetGitCAS at {}",
                                   StorageConfig::GitRoot().string());
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        auto git_api = GitApi{&repo};
        if (not git_api.RetrieveToCas(
                {Artifact::ObjectInfo{.digest = digest,
                                      .type = ObjectType::Tree}},
                &(*remote_api_))) {
            auto str = fmt::format(
                "Failed to sync tree {} from local Git cache", tree_id);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(ServeTreeResponse::SYNC_ERROR);
            return ::grpc::Status::OK;
        }
        // success!
        response->set_status(ServeTreeResponse::OK);
        return ::grpc::Status::OK;
    }
    // check if tree is in a known repository
    for (auto const& path : RemoteServeConfig::KnownRepositories()) {
        if (IsTreeInRepo(tree_id, path, logger_)) {
            // upload tree to remote CAS
            auto repo = RepositoryConfig{};
            if (not repo.SetGitCAS(path)) {
                auto str =
                    fmt::format("Failed to SetGitCAS at {}", path.string());
                logger_->Emit(LogLevel::Error, str);
                response->set_status(ServeTreeResponse::INTERNAL_ERROR);
                return ::grpc::Status::OK;
            }
            auto git_api = GitApi{&repo};
            if (not git_api.RetrieveToCas(
                    {Artifact::ObjectInfo{.digest = digest,
                                          .type = ObjectType::Tree}},
                    &(*remote_api_))) {
                auto str = fmt::format(
                    "Failed to sync tree {} from known repository {}",
                    tree_id,
                    path.string());
                logger_->Emit(LogLevel::Error, str);
                response->set_status(ServeTreeResponse::SYNC_ERROR);
                return ::grpc::Status::OK;
            }
            // success!
            response->set_status(ServeTreeResponse::OK);
            return ::grpc::Status::OK;
        }
    }
    // check also in the local CAS
    if (local_api_->IsAvailable(digest) and
        not local_api_->RetrieveToCas(
            {Artifact::ObjectInfo{.digest = digest, .type = ObjectType::Tree}},
            &(*remote_api_))) {
        auto str =
            fmt::format("Failed to sync tree {} from local CAS", tree_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(ServeTreeResponse::SYNC_ERROR);
        return ::grpc::Status::OK;
    }
    // tree not known
    response->set_status(ServeTreeResponse::NOT_FOUND);
    return ::grpc::Status::OK;
}

auto SourceTreeService::CheckRootTree(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::CheckRootTreeRequest* request,
    CheckRootTreeResponse* response) -> ::grpc::Status {
    auto const& tree_id{request->tree()};
    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, str);
        response->set_status(CheckRootTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // check first in the Git cache
    if (IsTreeInRepo(tree_id, StorageConfig::GitRoot(), logger_)) {
        // success!
        response->set_status(CheckRootTreeResponse::OK);
        return ::grpc::Status::OK;
    }
    // check if tree is in a known repository
    for (auto const& path : RemoteServeConfig::KnownRepositories()) {
        if (IsTreeInRepo(tree_id, path, logger_)) {
            // success!
            response->set_status(CheckRootTreeResponse::OK);
            return ::grpc::Status::OK;
        }
    }
    // now check in the local CAS
    auto digest = ArtifactDigest{tree_id, 0, /*is_tree=*/true};
    if (auto path = Storage::Instance().CAS().TreePath(digest)) {
        // As we currently build only against roots in Git repositories, we need
        // to move the tree from CAS to local Git storage
        auto tmp_dir =
            StorageUtils::CreateTypedTmpDir("source-tree-check-root-tree");
        if (not tmp_dir) {
            auto str = fmt::format(
                "Failed to create tmp directory for copying "
                "git-tree {} from remote CAS",
                digest.hash());
            logger_->Emit(LogLevel::Error, str);
            response->set_status(CheckRootTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        if (not local_api_->RetrieveToPaths(
                {Artifact::ObjectInfo{.digest = digest,
                                      .type = ObjectType::Tree}},
                {tmp_dir->GetPath()})) {
            auto str = fmt::format("Failed to copy git-tree {} to {}",
                                   tree_id,
                                   tmp_dir->GetPath().string());
            logger_->Emit(LogLevel::Error, str);
            response->set_status(CheckRootTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        // Import from tmp dir to Git cache
        auto res = CommonImportToGit(
            tmp_dir->GetPath(),
            fmt::format("Content of tree {}", tree_id)  // message
        );
        if (res.index() == 0) {
            // report the error
            logger_->Emit(LogLevel::Error, std::get<0>(res));
            response->set_status(CheckRootTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        auto const& imported_tree_id = std::get<1>(res);
        // sanity check
        if (imported_tree_id != tree_id) {
            auto str = fmt::format(
                "Unexpected mismatch in imported tree:\nexpected {} but got {}",
                tree_id,
                imported_tree_id);
            logger_->Emit(LogLevel::Error, str);
            response->set_status(CheckRootTreeResponse::INTERNAL_ERROR);
            return ::grpc::Status::OK;
        }
        // success!
        response->set_status(CheckRootTreeResponse::OK);
        return ::grpc::Status::OK;
    }
    // tree not known
    response->set_status(CheckRootTreeResponse::NOT_FOUND);
    return ::grpc::Status::OK;
}

auto SourceTreeService::GetRemoteTree(
    ::grpc::ServerContext* /* context */,
    const ::justbuild::just_serve::GetRemoteTreeRequest* request,
    GetRemoteTreeResponse* response) -> ::grpc::Status {
    auto const& tree_id{request->tree()};
    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, str);
        response->set_status(GetRemoteTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // get tree from remote CAS into tmp dir
    auto digest = ArtifactDigest{tree_id, 0, /*is_tree=*/true};
    if (not remote_api_->IsAvailable(digest)) {
        auto str = fmt::format("Remote CAS does not contain expected tree {}",
                               tree_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(GetRemoteTreeResponse::FAILED_PRECONDITION);
        return ::grpc::Status::OK;
    }
    auto tmp_dir =
        StorageUtils::CreateTypedTmpDir("source-tree-get-remote-tree");
    if (not tmp_dir) {
        auto str = fmt::format(
            "Failed to create tmp directory for copying git-tree {} from "
            "remote CAS",
            digest.hash());
        logger_->Emit(LogLevel::Error, str);
        response->set_status(GetRemoteTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    if (not remote_api_->RetrieveToPaths(
            {Artifact::ObjectInfo{.digest = digest, .type = ObjectType::Tree}},
            {tmp_dir->GetPath()},
            &(*local_api_))) {
        auto str =
            fmt::format("Failed to retrieve tree {} from remote CAS", tree_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(GetRemoteTreeResponse::FAILED_PRECONDITION);
        return ::grpc::Status::OK;
    }
    // Import from tmp dir to Git cache
    auto res =
        CommonImportToGit(tmp_dir->GetPath(),
                          fmt::format("Content of tree {}", tree_id)  // message
        );
    if (res.index() == 0) {
        // report the error
        logger_->Emit(LogLevel::Error, std::get<0>(res));
        response->set_status(GetRemoteTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    auto const& imported_tree_id = std::get<1>(res);
    // sanity check
    if (imported_tree_id != tree_id) {
        auto str = fmt::format(
            "Unexpected mismatch in imported tree:\nexpected {}, but got {}",
            tree_id,
            imported_tree_id);
        logger_->Emit(LogLevel::Error, str);
        response->set_status(GetRemoteTreeResponse::INTERNAL_ERROR);
        return ::grpc::Status::OK;
    }
    // success!
    response->set_status(GetRemoteTreeResponse::OK);
    return ::grpc::Status::OK;
}
