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

#include "src/other_tools/root_maps/content_git_map.hpp"

#include "fmt/core.h"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/utils/content.hpp"
#include "src/utils/archive/archive_ops.hpp"

namespace {

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
    return "unrecognized repository type";
}

void ResolveContentTree(
    std::string const& content,
    std::string const& tree_hash,
    bool is_cache_hit,
    std::optional<PragmaSpecial> const& pragma_special,
    bool absent,
    bool fetch_absent,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& ws_setter,
    ContentGitMap::LoggerPtr const& logger) {
    if (pragma_special) {
        // get the resolved tree
        auto tree_id_file =
            StorageUtils::GetResolvedTreeIDFile(tree_hash, *pragma_special);
        if (FileSystemManager::Exists(tree_id_file)) {
            // read resolved tree id
            auto resolved_tree_id = FileSystemManager::ReadFile(tree_id_file);
            if (not resolved_tree_id) {
                (*logger)(fmt::format("Failed to read resolved "
                                      "tree id from file {}",
                                      tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            // set the workspace root
            auto root = nlohmann::json::array(
                {FileRoot::kGitTreeMarker, *resolved_tree_id});
            if (fetch_absent or not absent) {
                root.emplace_back(StorageConfig::GitRoot().string());
            }
            (*ws_setter)(std::pair(std::move(root), true));
        }
        else {
            // resolve tree
            resolve_symlinks_map->ConsumeAfterKeysReady(
                ts,
                {GitObjectToResolve(tree_hash,
                                    ".",
                                    *pragma_special,
                                    /*known_info=*/std::nullopt)},
                [resolve_symlinks_map,
                 tree_hash,
                 tree_id_file,
                 is_cache_hit,
                 absent,
                 fetch_absent,
                 ws_setter,
                 logger](auto const& hashes) {
                    if (not hashes[0]) {
                        // check for cycles
                        auto error = DetectAndReportCycle(*resolve_symlinks_map,
                                                          tree_hash);
                        if (error) {
                            (*logger)(fmt::format("Failed to resolve symlinks "
                                                  "in tree {}:\n{}",
                                                  tree_hash,
                                                  *error),
                                      /*fatal=*/true);
                            return;
                        }
                        (*logger)(fmt::format("Unknown error in resolving "
                                              "symlinks in tree {}",
                                              tree_hash),
                                  /*fatal=*/true);
                        return;
                    }
                    auto const& resolved_tree = *hashes[0];
                    // cache the resolved tree in the CAS map
                    if (not StorageUtils::WriteTreeIDFile(tree_id_file,
                                                          resolved_tree.id)) {
                        (*logger)(fmt::format("Failed to write resolved tree "
                                              "id to file {}",
                                              tree_id_file.string()),
                                  /*fatal=*/true);
                        return;
                    }
                    // set the workspace root
                    auto root = nlohmann::json::array(
                        {FileRoot::kGitTreeMarker, resolved_tree.id});
                    if (fetch_absent or not absent) {
                        root.emplace_back(StorageConfig::GitRoot().string());
                    }
                    (*ws_setter)(std::pair(std::move(root), is_cache_hit));
                },
                [logger, content](auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While resolving symlinks for "
                                          "content {}:\n{}",
                                          content,
                                          msg),
                              fatal);
                });
        }
    }
    else {
        // set the workspace root as-is
        auto root =
            nlohmann::json::array({FileRoot::kGitTreeMarker, tree_hash});
        if (fetch_absent or not absent) {
            root.emplace_back(StorageConfig::GitRoot().string());
        }
        (*ws_setter)(std::pair(std::move(root), is_cache_hit));
    }
}

// Helper function for improved readability. It guarantees the logger is called
// exactly once with fatal if failure.
void WriteIdFileAndSetWSRoot(
    std::string const& archive_tree_id,
    GitCASPtr const& just_git_cas,
    std::filesystem::path const& archive_tree_id_file,
    std::string const& content_id,
    std::string const& subdir,
    std::optional<PragmaSpecial> const& pragma_special,
    bool absent,
    bool fetch_absent,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    // write to tree id file
    if (not StorageUtils::WriteTreeIDFile(archive_tree_id_file,
                                          archive_tree_id)) {
        (*logger)(fmt::format("Failed to write tree id to file {}",
                              archive_tree_id_file.string()),
                  /*fatal=*/true);
        return;
    }
    // we look for subtree in Git cache
    auto just_git_repo = GitRepoRemote::Open(just_git_cas);
    if (not just_git_repo) {
        (*logger)("Could not open Git cache repository!",
                  /*fatal=*/true);
        return;
    }
    // setup wrapped logger
    auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
        [&logger, subdir, tree = archive_tree_id](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While getting subdir {} from tree {}:\n{}",
                                  subdir,
                                  tree,
                                  msg),
                      fatal);
        });
    // get subtree id
    auto subtree_hash = just_git_repo->GetSubtreeFromTree(
        archive_tree_id, subdir, wrapped_logger);
    if (not subtree_hash) {
        return;
    }
    // resolve tree and set workspace root
    ResolveContentTree(content_id,
                       *subtree_hash,
                       false, /*is_cache_hit*/
                       pragma_special,
                       absent,
                       fetch_absent,
                       resolve_symlinks_map,
                       ts,
                       setter,
                       logger);
}

// Helper function for improved readability. It guarantees the logger is called
// exactly once with fatal if failure.
void ExtractAndImportToGit(
    std::filesystem::path const& content_cas_path,
    std::filesystem::path const& archive_tree_id_file,
    std::string const& repo_type,
    std::string const& content_id,
    std::string const& subdir,
    std::optional<PragmaSpecial> const& pragma_special,
    bool absent,
    bool fetch_absent,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    // extract archive
    auto tmp_dir = StorageUtils::CreateTypedTmpDir(repo_type);
    if (not tmp_dir) {
        (*logger)(fmt::format("Failed to create tmp path for {} target {}",
                              repo_type,
                              content_id),
                  /*fatal=*/true);
        return;
    }
    auto res = ExtractArchive(content_cas_path, repo_type, tmp_dir->GetPath());
    if (res != std::nullopt) {
        (*logger)(fmt::format("Failed to extract archive {} "
                              "from CAS with error: {}",
                              content_cas_path.string(),
                              *res),
                  /*fatal=*/true);
        return;
    }
    // import to git
    CommitInfo c_info{tmp_dir->GetPath(), repo_type, content_id};
    import_to_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(c_info)},
        [tmp_dir,  // keep tmp_dir alive
         archive_tree_id_file,
         content_id,
         subdir,
         pragma_special,
         absent,
         fetch_absent,
         resolve_symlinks_map,
         ts,
         setter,
         logger](auto const& values) {
            // check for errors
            if (not values[0]->second) {
                (*logger)("Importing to git failed",
                          /*fatal=*/true);
                return;
            }
            // only tree id is needed
            std::string archive_tree_id = values[0]->first;
            // open Git CAS
            auto just_git_cas = GitCAS::Open(StorageConfig::GitRoot());
            if (not just_git_cas) {
                (*logger)("Could not open Git cache object database!",
                          /*fatal=*/true);
                return;
            }
            // write to id file and process subdir tree
            WriteIdFileAndSetWSRoot(archive_tree_id,
                                    just_git_cas,
                                    archive_tree_id_file,
                                    content_id,
                                    subdir,
                                    pragma_special,
                                    absent,
                                    fetch_absent,
                                    resolve_symlinks_map,
                                    ts,
                                    setter,
                                    logger);
        },
        [logger, target_path = tmp_dir->GetPath()](auto const& msg,
                                                   bool fatal) {
            (*logger)(fmt::format("While importing target {} "
                                  "to Git:\n{}",
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}

}  // namespace

auto CreateContentGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    LocalPathsPtr const& just_mr_paths,
    MirrorsPtr const& additional_mirrors,
    CAInfoPtr const& ca_info,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    bool serve_api_exists,
    IExecutionApi* local_api,
    IExecutionApi* remote_api,
    bool fetch_absent,
    std::size_t jobs) -> ContentGitMap {
    auto gitify_content = [content_cas_map,
                           import_to_git_map,
                           resolve_symlinks_map,
                           critical_git_op_map,
                           just_mr_paths,
                           additional_mirrors,
                           ca_info,
                           serve_api_exists,
                           local_api,
                           remote_api,
                           fetch_absent](auto ts,
                                         auto setter,
                                         auto logger,
                                         auto /* unused */,
                                         auto const& key) {
        auto archive_tree_id_file = StorageUtils::GetArchiveTreeIDFile(
            key.repo_type, key.archive.content);
        if (FileSystemManager::Exists(archive_tree_id_file)) {
            // read archive_tree_id from file tree_id_file
            auto archive_tree_id =
                FileSystemManager::ReadFile(archive_tree_id_file);
            if (not archive_tree_id) {
                (*logger)(fmt::format("Failed to read tree id from file {}",
                                      archive_tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            // ensure Git cache
            // define Git operation to be done
            GitOpKey op_key = {.params =
                                   {
                                       StorageConfig::GitRoot(),  // target_path
                                       "",                        // git_hash
                                       "",                        // branch
                                       std::nullopt,              // message
                                       true                       // init_bare
                                   },
                               .op_type = GitOpType::ENSURE_INIT};
            critical_git_op_map->ConsumeAfterKeysReady(
                ts,
                {std::move(op_key)},
                [archive_tree_id = *archive_tree_id,
                 subdir = key.subdir,
                 content = key.archive.content,
                 pragma_special = key.pragma_special,
                 absent = key.absent,
                 fetch_absent,
                 resolve_symlinks_map,
                 ts,
                 setter,
                 logger](auto const& values) {
                    GitOpValue op_result = *values[0];
                    // check flag
                    if (not op_result.result) {
                        (*logger)("Git init failed",
                                  /*fatal=*/true);
                        return;
                    }
                    // open fake repo wrap for GitCAS
                    auto just_git_repo = GitRepoRemote::Open(op_result.git_cas);
                    if (not just_git_repo) {
                        (*logger)("Could not open Git cache repository!",
                                  /*fatal=*/true);
                        return;
                    }
                    // setup wrapped logger
                    auto wrapped_logger =
                        std::make_shared<AsyncMapConsumerLogger>(
                            [&logger](auto const& msg, bool fatal) {
                                (*logger)(
                                    fmt::format("While getting subtree from "
                                                "tree:\n{}",
                                                msg),
                                    fatal);
                            });
                    // get subtree id
                    auto subtree_hash = just_git_repo->GetSubtreeFromTree(
                        archive_tree_id, subdir, wrapped_logger);
                    if (not subtree_hash) {
                        return;
                    }
                    // resolve tree and set workspace root
                    ResolveContentTree(content,
                                       *subtree_hash,
                                       true, /*is_cache_hit*/
                                       pragma_special,
                                       absent,
                                       fetch_absent,
                                       resolve_symlinks_map,
                                       ts,
                                       setter,
                                       logger);
                },
                [logger, target_path = StorageConfig::GitRoot()](
                    auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While running critical Git "
                                          "op ENSURE_INIT for "
                                          "target {}:\n{}",
                                          target_path.string(),
                                          msg),
                              fatal);
                });
        }
        else {
            // check if content already in CAS
            auto const& cas = Storage::Instance().CAS();
            auto digest = ArtifactDigest(key.archive.content, 0, false);
            if (auto content_cas_path =
                    cas.BlobPath(digest, /*is_executable=*/false)) {
                ExtractAndImportToGit(*content_cas_path,
                                      archive_tree_id_file,
                                      key.repo_type,
                                      key.archive.content,
                                      key.subdir,
                                      key.pragma_special,
                                      key.absent,
                                      fetch_absent,
                                      import_to_git_map,
                                      resolve_symlinks_map,
                                      ts,
                                      setter,
                                      logger);
                // done
                return;
            }
            // check if content is in Git cache;
            // ensure Git cache
            GitOpKey op_key = {.params =
                                   {
                                       StorageConfig::GitRoot(),  // target_path
                                       "",                        // git_hash
                                       "",                        // branch
                                       std::nullopt,              // message
                                       true                       // init_bare
                                   },
                               .op_type = GitOpType::ENSURE_INIT};
            critical_git_op_map->ConsumeAfterKeysReady(
                ts,
                {std::move(op_key)},
                [key,
                 digest,
                 archive_tree_id_file,
                 content_cas_map,
                 import_to_git_map,
                 resolve_symlinks_map,
                 just_mr_paths,
                 additional_mirrors,
                 ca_info,
                 serve_api_exists,
                 local_api,
                 remote_api,
                 fetch_absent,
                 ts,
                 setter,
                 logger](auto const& values) {
                    GitOpValue op_result = *values[0];
                    // check flag
                    if (not op_result.result) {
                        (*logger)("Git init failed",
                                  /*fatal=*/true);
                        return;
                    }
                    auto const just_git_cas = op_result.git_cas;
                    // open fake repo wrap for GitCAS
                    auto just_git_repo = GitRepoRemote::Open(just_git_cas);
                    if (not just_git_repo) {
                        (*logger)("Could not open Git cache repository!",
                                  /*fatal=*/true);
                        return;
                    }
                    // verify if local Git knows content blob
                    auto wrapped_logger =
                        std::make_shared<AsyncMapConsumerLogger>(
                            [&logger, blob = key.archive.content](
                                auto const& msg, bool fatal) {
                                (*logger)(
                                    fmt::format("While verifying presence of "
                                                "blob {}:\n{}",
                                                blob,
                                                msg),
                                    fatal);
                            });
                    auto res = just_git_repo->TryReadBlob(key.archive.content,
                                                          wrapped_logger);
                    if (not res.first) {
                        // blob check failed
                        return;
                    }
                    auto const& cas = Storage::Instance().CAS();
                    if (res.second) {
                        // blob found; add it to CAS
                        if (not cas.StoreBlob(*res.second,
                                              /*is_executable=*/false)) {
                            (*logger)(fmt::format("Failed to store content {} "
                                                  "to local CAS",
                                                  key.archive.content),
                                      /*fatal=*/true);
                            return;
                        }
                        if (auto content_cas_path =
                                cas.BlobPath(digest, /*is_executable=*/false)) {
                            ExtractAndImportToGit(*content_cas_path,
                                                  archive_tree_id_file,
                                                  key.repo_type,
                                                  key.archive.content,
                                                  key.subdir,
                                                  key.pragma_special,
                                                  key.absent,
                                                  fetch_absent,
                                                  import_to_git_map,
                                                  resolve_symlinks_map,
                                                  ts,
                                                  setter,
                                                  logger);
                            // done
                            return;
                        }
                        // this should normally never be reached unless
                        // something went really wrong
                        (*logger)(fmt::format("Failed to retrieve blob {} from "
                                              "local CAS",
                                              digest.hash()),
                                  /*fatal=*/true);
                        return;
                    }
                    JustMRProgress::Instance().TaskTracker().Start(
                        key.archive.origin);
                    // add distfile to CAS
                    auto repo_distfile =
                        (key.archive.distfile
                             ? key.archive.distfile.value()
                             : std::filesystem::path(key.archive.fetch_url)
                                   .filename()
                                   .string());
                    StorageUtils::AddDistfileToCAS(repo_distfile,
                                                   just_mr_paths);
                    // check if content is in CAS now
                    if (auto content_cas_path =
                            cas.BlobPath(digest, /*is_executable=*/false)) {
                        JustMRProgress::Instance().TaskTracker().Stop(
                            key.archive.origin);
                        ExtractAndImportToGit(*content_cas_path,
                                              archive_tree_id_file,
                                              key.repo_type,
                                              key.archive.content,
                                              key.subdir,
                                              key.pragma_special,
                                              key.absent,
                                              fetch_absent,
                                              import_to_git_map,
                                              resolve_symlinks_map,
                                              ts,
                                              setter,
                                              logger);
                        // done
                        return;
                    }
                    // check if content is known to remote serve service
                    if (serve_api_exists) {
                        // if purely absent, request the resolved subdir tree
                        // directly
                        if (key.absent and not fetch_absent) {
                            if (auto tree_id =
                                    ServeApi::RetrieveTreeFromArchive(
                                        key.archive.content,
                                        key.repo_type,
                                        key.subdir,
                                        key.pragma_special,
                                        /*sync_tree = */ false)) {
                                // set the workspace root as absent
                                JustMRProgress::Instance().TaskTracker().Stop(
                                    key.archive.origin);
                                (*setter)(std::pair(
                                    nlohmann::json::array(
                                        {FileRoot::kGitTreeMarker, *tree_id}),
                                    /*is_cache_hit = */ false));
                                return;
                            }
                            // give warning
                            (*logger)(
                                fmt::format("Tree at subdir {} for archive {} "
                                            "could not be served",
                                            key.subdir,
                                            key.archive.content),
                                /*fatal=*/false);
                        }
                        // otherwise, request (and sync) the whole archive tree,
                        // UNRESOLVED, to ensure we maintain the id file
                        // association
                        else {
                            if (auto root_tree_id =
                                    ServeApi::RetrieveTreeFromArchive(
                                        key.archive.content,
                                        key.repo_type,
                                        /*subdir = */ ".",
                                        /* resolve_symlinks = */ std::nullopt,
                                        /*sync_tree = */ true)) {
                                // verify if we already know the tree locally;
                                // setup wrapped logger
                                auto wrapped_logger =
                                    std::make_shared<AsyncMapConsumerLogger>(
                                        [&logger, tree = *root_tree_id](
                                            auto const& msg, bool fatal) {
                                            (*logger)(
                                                fmt::format(
                                                    "While verifying presence "
                                                    "of tree {}:\n{}",
                                                    tree,
                                                    msg),
                                                fatal);
                                        });
                                auto tree_present =
                                    just_git_repo->CheckTreeExists(
                                        *root_tree_id, wrapped_logger);
                                if (not tree_present) {
                                    return;
                                }
                                if (*tree_present) {
                                    JustMRProgress::Instance()
                                        .TaskTracker()
                                        .Stop(key.archive.origin);
                                    // write to id file and process subdir tree
                                    WriteIdFileAndSetWSRoot(
                                        *root_tree_id,
                                        just_git_cas,
                                        archive_tree_id_file,
                                        key.archive.content,
                                        key.subdir,
                                        key.pragma_special,
                                        key.absent,
                                        fetch_absent,
                                        resolve_symlinks_map,
                                        ts,
                                        setter,
                                        logger);
                                    // done
                                    return;
                                }
                                // try to get root tree from remote execution
                                // endpoint
                                auto root_digest = ArtifactDigest{
                                    *root_tree_id, 0, /*is_tree=*/true};
                                if (remote_api != nullptr and
                                    local_api != nullptr and
                                    remote_api->RetrieveToCas(
                                        {Artifact::ObjectInfo{
                                            .digest = root_digest,
                                            .type = ObjectType::Tree}},
                                        local_api)) {
                                    JustMRProgress::Instance()
                                        .TaskTracker()
                                        .Stop(key.archive.origin);
                                    // Move tree from CAS to local git storage
                                    auto tmp_dir =
                                        StorageUtils::CreateTypedTmpDir(
                                            "fetch-absent-root");
                                    if (not tmp_dir) {
                                        (*logger)(
                                            fmt::format(
                                                "Failed to create tmp "
                                                "directory after fetching root "
                                                "tree {} for absent archive {}",
                                                *root_tree_id,
                                                key.archive.content),
                                            true);
                                        return;
                                    }
                                    if (not local_api->RetrieveToPaths(
                                            {Artifact::ObjectInfo{
                                                .digest = root_digest,
                                                .type = ObjectType::Tree}},
                                            {tmp_dir->GetPath()})) {
                                        (*logger)(
                                            fmt::format(
                                                "Failed to copy fetched root "
                                                "tree {} to {}",
                                                *root_tree_id,
                                                tmp_dir->GetPath().string()),
                                            true);
                                        return;
                                    }
                                    CommitInfo c_info{tmp_dir->GetPath(),
                                                      "tree",
                                                      *root_tree_id};
                                    import_to_git_map->ConsumeAfterKeysReady(
                                        ts,
                                        {std::move(c_info)},
                                        [tmp_dir,  // keep tmp_dir alive
                                         root_tree_id,
                                         content = key.archive.content,
                                         subdir = key.subdir,
                                         pragma_special = key.pragma_special,
                                         absent = key.absent,
                                         fetch_absent,
                                         just_git_cas,
                                         archive_tree_id_file,
                                         resolve_symlinks_map,
                                         ts,
                                         setter,
                                         logger](auto const& values) {
                                            if (not values[0]->second) {
                                                (*logger)(
                                                    "Importing to git failed",
                                                    /*fatal=*/true);
                                                return;
                                            }
                                            // write to id file and process
                                            // subdir tree
                                            WriteIdFileAndSetWSRoot(
                                                *root_tree_id,
                                                just_git_cas,
                                                archive_tree_id_file,
                                                content,
                                                subdir,
                                                pragma_special,
                                                absent,
                                                fetch_absent,
                                                resolve_symlinks_map,
                                                ts,
                                                setter,
                                                logger);
                                        },
                                        [logger, tmp_dir, root_tree_id](
                                            auto const& msg, bool fatal) {
                                            (*logger)(
                                                fmt::format(
                                                    "While moving root tree {} "
                                                    "from {} to local git:\n{}",
                                                    *root_tree_id,
                                                    tmp_dir->GetPath().string(),
                                                    msg),
                                                fatal);
                                        });
                                    // done
                                    return;
                                }
                                // try to fetch content from network
                                content_cas_map->ConsumeAfterKeysReady(
                                    ts,
                                    {key.archive},
                                    [archive_tree_id_file,
                                     archive_origin = key.archive.origin,
                                     repo_type = key.repo_type,
                                     content_id = key.archive.content,
                                     subdir = key.subdir,
                                     pragma_special = key.pragma_special,
                                     absent = key.absent,
                                     fetch_absent,
                                     import_to_git_map,
                                     resolve_symlinks_map,
                                     ts,
                                     setter,
                                     logger](
                                        [[maybe_unused]] auto const& values) {
                                        JustMRProgress::Instance()
                                            .TaskTracker()
                                            .Stop(archive_origin);
                                        // content is in CAS
                                        auto const& cas =
                                            Storage::Instance().CAS();
                                        auto content_cas_path =
                                            cas.BlobPath(
                                                   ArtifactDigest(
                                                       content_id, 0, false),
                                                   /*is_executable=*/
                                                   false)
                                                .value();
                                        ExtractAndImportToGit(
                                            content_cas_path,
                                            archive_tree_id_file,
                                            repo_type,
                                            content_id,
                                            subdir,
                                            pragma_special,
                                            absent,
                                            fetch_absent,
                                            import_to_git_map,
                                            resolve_symlinks_map,
                                            ts,
                                            setter,
                                            logger);
                                    },
                                    [logger, content = key.archive.content](
                                        auto const& msg, bool fatal) {
                                        (*logger)(fmt::format(
                                                      "While ensuring content "
                                                      "{} is in CAS:\n{}",
                                                      content,
                                                      msg),
                                                  fatal);
                                    });
                                // done
                                return;
                            }
                            // give warning
                            (*logger)(fmt::format("Root tree for content {} "
                                                  "could not be served",
                                                  key.archive.content),
                                      /*fatal=*/false);
                        }
                    }
                    else {
                        if (key.absent) {
                            // give warning
                            (*logger)(
                                fmt::format("Missing serve endpoint for "
                                            "content {} marked absent requires "
                                            "slower network fetch.",
                                            key.archive.content),
                                /*fatal=*/false);
                        }
                    }
                    // revert to network fetch
                    content_cas_map->ConsumeAfterKeysReady(
                        ts,
                        {key.archive},
                        [archive_tree_id_file,
                         archive_origin = key.archive.origin,
                         repo_type = key.repo_type,
                         content_id = key.archive.content,
                         subdir = key.subdir,
                         pragma_special = key.pragma_special,
                         absent = key.absent,
                         fetch_absent,
                         import_to_git_map,
                         resolve_symlinks_map,
                         ts,
                         setter,
                         logger]([[maybe_unused]] auto const& values) {
                            JustMRProgress::Instance().TaskTracker().Stop(
                                archive_origin);
                            // content is in CAS
                            auto const& cas = Storage::Instance().CAS();
                            auto content_cas_path =
                                cas.BlobPath(
                                       ArtifactDigest(content_id, 0, false),
                                       /*is_executable=*/false)
                                    .value();
                            ExtractAndImportToGit(content_cas_path,
                                                  archive_tree_id_file,
                                                  repo_type,
                                                  content_id,
                                                  subdir,
                                                  pragma_special,
                                                  absent,
                                                  fetch_absent,
                                                  import_to_git_map,
                                                  resolve_symlinks_map,
                                                  ts,
                                                  setter,
                                                  logger);
                        },
                        [logger, content = key.archive.content](auto const& msg,
                                                                bool fatal) {
                            (*logger)(fmt::format("While ensuring content {} "
                                                  "is in CAS:\n{}",
                                                  content,
                                                  msg),
                                      fatal);
                        });
                },
                [logger, target_path = StorageConfig::GitRoot()](
                    auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While running critical Git op "
                                          "ENSURE_INIT for target {}:\n{}",
                                          target_path.string(),
                                          msg),
                              fatal);
                });
        }
    };
    return AsyncMapConsumer<ArchiveRepoInfo, std::pair<nlohmann::json, bool>>(
        gitify_content, jobs);
}
