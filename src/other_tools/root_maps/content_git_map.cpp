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
#include "src/buildtool/multithreading/async_map_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/root_maps/root_utils.hpp"
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

/// \brief Helper function for ensuring the serve endpoint, if given, has the
/// root if it was marked absent.
/// It guarantees the logger is called exactly once with fatal on failure, and
/// the setter on success.
void EnsureRootAsAbsent(
    std::string const& tree_id,
    ArchiveRepoInfo const& key,
    bool serve_api_exists,
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
    bool is_cache_hit,
    ContentGitMap::SetterPtr const& ws_setter,
    ContentGitMap::LoggerPtr const& logger) {
    // this is an absent root
    if (serve_api_exists) {
        // check if the serve endpoint has this root
        auto has_tree = CheckServeHasAbsentRoot(tree_id, logger);
        if (not has_tree) {
            return;
        }
        if (not *has_tree) {
            // try to see if serve endpoint has the information to prepare the
            // root itself; this is redundant if root is not already cached
            if (is_cache_hit) {
                auto serve_result =
                    ServeApi::RetrieveTreeFromArchive(key.archive.content,
                                                      key.repo_type,
                                                      key.subdir,
                                                      key.pragma_special,
                                                      /*sync_tree=*/false);
                if (std::holds_alternative<std::string>(serve_result)) {
                    // if serve has set up the tree, it must match what we
                    // expect
                    auto const& served_tree_id =
                        std::get<std::string>(serve_result);
                    if (tree_id != served_tree_id) {
                        (*logger)(fmt::format("Mismatch in served root tree "
                                              "id:\nexpected {}, but got {}",
                                              tree_id,
                                              served_tree_id),
                                  /*fatal=*/true);
                        return;
                    }
                }
                else {
                    // check if serve failure was due to archive content not
                    // being found or it is otherwise fatal
                    auto const& is_fatal = std::get<bool>(serve_result);
                    if (is_fatal) {
                        (*logger)(
                            fmt::format("Serve endpoint failed to set up "
                                        "root from known archive content {}",
                                        key.archive.content),
                            /*fatal=*/true);
                        return;
                    }
                    if (not remote_api) {
                        (*logger)(
                            fmt::format(
                                "Missing or incompatible remote-execution "
                                "endpoint needed to sync workspace root {} "
                                "with the serve endpoint.",
                                tree_id),
                            /*fatal=*/true);
                        return;
                    }
                    // the tree is known locally, so we can upload it to remote
                    // CAS for the serve endpoint to retrieve it and set up the
                    // root
                    if (not EnsureAbsentRootOnServe(
                            tree_id,
                            StorageConfig::GitRoot(),
                            *remote_api,
                            logger,
                            /*no_sync_is_fatal=*/true)) {
                        return;
                    }
                }
            }
            else {
                // the tree is known locally, so we can upload it to remote CAS
                // for the serve endpoint to retrieve it and set up the root
                if (not remote_api) {
                    (*logger)(
                        fmt::format("Missing or incompatible remote-execution "
                                    "endpoint needed to sync workspace root {} "
                                    "with the serve endpoint.",
                                    tree_id),
                        /*fatal=*/true);
                    return;
                }
                // the tree is known locally, so we can upload it to remote
                // CAS for the serve endpoint to retrieve it and set up the
                // root
                if (not EnsureAbsentRootOnServe(tree_id,
                                                StorageConfig::GitRoot(),
                                                *remote_api,
                                                logger,
                                                /*no_sync_is_fatal=*/true)) {
                    return;
                }
            }
        }
    }
    else {
        // give warning
        (*logger)(fmt::format("Workspace root {} marked absent but no suitable "
                              "serve endpoint provided.",
                              tree_id),
                  /*fatal=*/false);
    }
    // set root as absent
    (*ws_setter)(
        std::pair(nlohmann::json::array({FileRoot::kGitTreeMarker, tree_id}),
                  /*is_cache_hit=*/is_cache_hit));
}

/// \brief Called to get the resolved root (with respect to symlinks) from
/// an unresolved tree. It guarantees the logger is called exactly once with
/// fatal on failure, and the setter on success.
void ResolveContentTree(
    ArchiveRepoInfo const& key,
    std::string const& tree_hash,
    GitCASPtr const& just_git_cas,
    bool is_cache_hit,
    bool is_absent,
    bool serve_api_exists,
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& ws_setter,
    ContentGitMap::LoggerPtr const& logger) {
    if (key.pragma_special) {
        // get the resolved tree
        auto tree_id_file =
            StorageUtils::GetResolvedTreeIDFile(tree_hash, *key.pragma_special);
        if (FileSystemManager::Exists(tree_id_file)) {
            // read resolved tree id
            auto resolved_tree_id = FileSystemManager::ReadFile(tree_id_file);
            if (not resolved_tree_id) {
                (*logger)(fmt::format("Failed to read resolved tree id "
                                      "from file {}",
                                      tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            // set the workspace root
            if (is_absent) {
                EnsureRootAsAbsent(*resolved_tree_id,
                                   key,
                                   serve_api_exists,
                                   remote_api,
                                   is_cache_hit,
                                   ws_setter,
                                   logger);
            }
            else {
                (*ws_setter)(std::pair(
                    nlohmann::json::array({FileRoot::kGitTreeMarker,
                                           *resolved_tree_id,
                                           StorageConfig::GitRoot().string()}),
                    /*is_cache_hit=*/is_cache_hit));
            }
        }
        else {
            // resolve tree; both source and target repos are the Git cache
            resolve_symlinks_map->ConsumeAfterKeysReady(
                ts,
                {GitObjectToResolve(tree_hash,
                                    ".",
                                    *key.pragma_special,
                                    /*known_info=*/std::nullopt,
                                    just_git_cas,
                                    just_git_cas)},
                [resolve_symlinks_map,
                 tree_hash,
                 tree_id_file,
                 is_cache_hit,
                 key,
                 is_absent,
                 serve_api_exists,
                 remote_api,
                 ws_setter,
                 logger](auto const& hashes) {
                    if (not hashes[0]) {
                        // check for cycles
                        if (auto error = DetectAndReportCycle(
                                fmt::format("resolving Git tree {}", tree_hash),
                                *resolve_symlinks_map,
                                kGitObjectToResolvePrinter)) {
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
                    if (is_absent) {
                        EnsureRootAsAbsent(resolved_tree.id,
                                           key,
                                           serve_api_exists,
                                           remote_api,
                                           is_cache_hit,
                                           ws_setter,
                                           logger);
                    }
                    else {
                        (*ws_setter)(
                            std::pair(nlohmann::json::array(
                                          {FileRoot::kGitTreeMarker,
                                           resolved_tree.id,
                                           StorageConfig::GitRoot().string()}),
                                      /*is_cache_hit=*/is_cache_hit));
                    }
                },
                [logger, content = key.archive.content](auto const& msg,
                                                        bool fatal) {
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
        if (is_absent) {
            EnsureRootAsAbsent(tree_hash,
                               key,
                               serve_api_exists,
                               remote_api,
                               is_cache_hit,
                               ws_setter,
                               logger);
        }
        else {
            (*ws_setter)(std::pair(
                nlohmann::json::array({FileRoot::kGitTreeMarker,
                                       tree_hash,
                                       StorageConfig::GitRoot().string()}),
                /*is_cache_hit=*/is_cache_hit));
        }
    }
}

/// \brief Called to store the file association and then set the root.
/// It guarantees the logger is called exactly once with fatal on failure,
/// and the setter on success.
void WriteIdFileAndSetWSRoot(
    ArchiveRepoInfo const& key,
    std::string const& archive_tree_id,
    GitCASPtr const& just_git_cas,
    std::filesystem::path const& archive_tree_id_file,
    bool is_absent,
    bool serve_api_exists,
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
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
        [&logger, subdir = key.subdir, tree = archive_tree_id](auto const& msg,
                                                               bool fatal) {
            (*logger)(fmt::format("While getting subdir {} from tree {}:\n{}",
                                  subdir,
                                  tree,
                                  msg),
                      fatal);
        });
    // get subtree id
    auto subtree_hash = just_git_repo->GetSubtreeFromTree(
        archive_tree_id, key.subdir, wrapped_logger);
    if (not subtree_hash) {
        return;
    }
    // resolve tree and set workspace root
    ResolveContentTree(key,
                       *subtree_hash,
                       just_git_cas,
                       false, /*is_cache_hit*/
                       is_absent,
                       serve_api_exists,
                       remote_api,
                       resolve_symlinks_map,
                       ts,
                       setter,
                       logger);
}

/// \brief Called when archive is in local CAS. Performs the import-to-git
/// and follow-up processing. It guarantees the logger is called exactly
/// once with fatal on failure, and the setter on success.
void ExtractAndImportToGit(
    ArchiveRepoInfo const& key,
    std::filesystem::path const& content_cas_path,
    std::filesystem::path const& archive_tree_id_file,
    bool is_absent,
    bool serve_api_exists,
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    // extract archive
    auto tmp_dir = StorageConfig::CreateTypedTmpDir(key.repo_type);
    if (not tmp_dir) {
        (*logger)(fmt::format("Failed to create tmp path for {} target {}",
                              key.repo_type,
                              key.archive.content),
                  /*fatal=*/true);
        return;
    }
    auto res =
        ExtractArchive(content_cas_path, key.repo_type, tmp_dir->GetPath());
    if (res != std::nullopt) {
        (*logger)(fmt::format("Failed to extract archive {} from CAS with "
                              "error:\n{}",
                              content_cas_path.string(),
                              *res),
                  /*fatal=*/true);
        return;
    }
    // import to git
    CommitInfo c_info{tmp_dir->GetPath(), key.repo_type, key.archive.content};
    import_to_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(c_info)},
        [tmp_dir,  // keep tmp_dir alive
         archive_tree_id_file,
         key,
         is_absent,
         serve_api_exists,
         remote_api,
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
            // write to id file and process subdir tree
            WriteIdFileAndSetWSRoot(key,
                                    archive_tree_id,
                                    values[0]->second, /*just_git_cas*/
                                    archive_tree_id_file,
                                    is_absent,
                                    serve_api_exists,
                                    remote_api,
                                    resolve_symlinks_map,
                                    ts,
                                    setter,
                                    logger);
        },
        [logger, target_path = tmp_dir->GetPath()](auto const& msg,
                                                   bool fatal) {
            (*logger)(fmt::format("While importing target {} to Git:\n{}",
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
    std::optional<gsl::not_null<IExecutionApi*>> const& remote_api,
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
                 key,
                 fetch_absent,
                 serve_api_exists,
                 remote_api,
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
                        archive_tree_id, key.subdir, wrapped_logger);
                    if (not subtree_hash) {
                        return;
                    }
                    // resolve tree and set workspace root (present or absent)
                    ResolveContentTree(
                        key,
                        *subtree_hash,
                        op_result.git_cas,
                        /*is_cache_hit = */ true,
                        /*is_absent = */ (key.absent and not fetch_absent),
                        serve_api_exists,
                        remote_api,
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
            // separate logic between absent and present roots
            if (key.absent and not fetch_absent) {
                // request the resolved subdir tree from the serve endpoint, if
                // given
                if (serve_api_exists) {
                    auto serve_result = ServeApi::RetrieveTreeFromArchive(
                        key.archive.content,
                        key.repo_type,
                        key.subdir,
                        key.pragma_special,
                        /*sync_tree = */ false);
                    if (std::holds_alternative<std::string>(serve_result)) {
                        // set the workspace root as absent
                        JustMRProgress::Instance().TaskTracker().Stop(
                            key.archive.origin);
                        (*setter)(std::pair(
                            nlohmann::json::array(
                                {FileRoot::kGitTreeMarker,
                                 std::get<std::string>(serve_result)}),
                            /*is_cache_hit = */ false));
                        return;
                    }
                    // check if serve failure was due to archive content
                    // not being found or it is otherwise fatal
                    auto const& is_fatal = std::get<bool>(serve_result);
                    if (is_fatal) {
                        (*logger)(
                            fmt::format("Serve endpoint failed to set up root "
                                        "from known archive content {}",
                                        key.archive.content),
                            /*fatal=*/true);
                        return;
                    }
                }
                // if serve endpoint cannot set up the root, we might still be
                // able to set up the absent root with local information, and if
                // a serve endpoint exists we can upload it the root ourselves;

                // check if content already in CAS
                auto const& cas = Storage::Instance().CAS();
                auto digest = ArtifactDigest(key.archive.content, 0, false);
                if (auto content_cas_path =
                        cas.BlobPath(digest, /*is_executable=*/false)) {
                    ExtractAndImportToGit(key,
                                          *content_cas_path,
                                          archive_tree_id_file,
                                          /*is_absent = */ true,
                                          serve_api_exists,
                                          remote_api,
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
                GitOpKey op_key = {
                    .params =
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
                     import_to_git_map,
                     resolve_symlinks_map,
                     just_mr_paths,
                     additional_mirrors,
                     ca_info,
                     serve_api_exists,
                     remote_api,
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
                                        fmt::format("While verifying presence "
                                                    "of blob {}:\n{}",
                                                    blob,
                                                    msg),
                                        fatal);
                                });
                        auto res = just_git_repo->TryReadBlob(
                            key.archive.content, wrapped_logger);
                        if (not res.first) {
                            // blob check failed
                            return;
                        }
                        auto const& cas = Storage::Instance().CAS();
                        if (res.second) {
                            // blob found; add it to CAS
                            if (not cas.StoreBlob(*res.second,
                                                  /*is_executable=*/false)) {
                                (*logger)(fmt::format("Failed to store content "
                                                      "{} to local CAS",
                                                      key.archive.content),
                                          /*fatal=*/true);
                                return;
                            }
                            if (auto content_cas_path = cas.BlobPath(
                                    digest, /*is_executable=*/false)) {
                                ExtractAndImportToGit(key,
                                                      *content_cas_path,
                                                      archive_tree_id_file,
                                                      /*is_absent=*/true,
                                                      serve_api_exists,
                                                      remote_api,
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
                            (*logger)(fmt::format("Failed to retrieve blob {} "
                                                  "from local CAS",
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
                            ExtractAndImportToGit(key,
                                                  *content_cas_path,
                                                  archive_tree_id_file,
                                                  /*is_absent=*/true,
                                                  serve_api_exists,
                                                  remote_api,
                                                  import_to_git_map,
                                                  resolve_symlinks_map,
                                                  ts,
                                                  setter,
                                                  logger);
                            // done
                            return;
                        }
                        // report not being able to set up this root as absent
                        (*logger)(fmt::format("Cannot create workspace root as "
                                              "absent for content {}.",
                                              key.archive.content),
                                  /*fatal=*/true);
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
            else {
                // for a present root we need the archive to be present too
                content_cas_map->ConsumeAfterKeysReady(
                    ts,
                    {key.archive},
                    [archive_tree_id_file,
                     key,
                     import_to_git_map,
                     resolve_symlinks_map,
                     ts,
                     setter,
                     logger]([[maybe_unused]] auto const& values) {
                        // content is in local CAS now
                        auto const& cas = Storage::Instance().CAS();
                        auto content_cas_path =
                            cas.BlobPath(ArtifactDigest(
                                             key.archive.content, 0, false),
                                         /*is_executable=*/false)
                                .value();
                        // root can only be present, so default all arguments
                        // that refer to a serve endpoint
                        ExtractAndImportToGit(key,
                                              content_cas_path,
                                              archive_tree_id_file,
                                              /*is_absent=*/false,
                                              /*serve_api_exists=*/false,
                                              /*remote_api=*/std::nullopt,
                                              import_to_git_map,
                                              resolve_symlinks_map,
                                              ts,
                                              setter,
                                              logger);
                    },
                    [logger, content = key.archive.content](auto const& msg,
                                                            bool fatal) {
                        (*logger)(fmt::format("While ensuring content {} is in "
                                              "CAS:\n{}",
                                              content,
                                              msg),
                                  fatal);
                    });
            }
        }
    };
    return AsyncMapConsumer<ArchiveRepoInfo, std::pair<nlohmann::json, bool>>(
        gitify_content, jobs);
}
