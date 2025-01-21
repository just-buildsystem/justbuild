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

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/crypto/hash_info.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
#include "src/buildtool/file_system/git_types.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/task_tracker.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/other_tools/git_operations/git_ops_types.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"
#include "src/other_tools/root_maps/root_utils.hpp"
#include "src/utils/archive/archive_ops.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

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
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    bool is_cache_hit,
    ContentGitMap::SetterPtr const& ws_setter,
    ContentGitMap::LoggerPtr const& logger) {
    // this is an absent root
    if (serve != nullptr) {
        // check if the serve endpoint has this root
        auto has_tree = CheckServeHasAbsentRoot(*serve, tree_id, logger);
        if (not has_tree) {
            return;
        }
        if (not *has_tree) {
            // try to see if serve endpoint has the information to prepare the
            // root itself; this is redundant if root is not already cached
            bool on_serve = false;
            if (is_cache_hit) {
                auto const serve_result = serve->RetrieveTreeFromArchive(
                    key.archive.content_hash.Hash(),
                    key.repo_type,
                    key.subdir,
                    key.pragma_special,
                    /*sync_tree=*/false);
                if (serve_result) {
                    // if serve has set up the tree, it must match what we
                    // expect
                    if (tree_id != serve_result->tree) {
                        (*logger)(fmt::format("Mismatch in served root tree "
                                              "id:\nexpected {}, but got {}",
                                              tree_id,
                                              serve_result->tree),
                                  /*fatal=*/true);
                        return;
                    }
                    on_serve = true;
                }

                if (not serve_result.has_value() and
                    serve_result.error() == GitLookupError::Fatal) {
                    (*logger)(fmt::format("Serve endpoint failed to set up "
                                          "root from known archive content {}",
                                          key.archive.content_hash.Hash()),
                              /*fatal=*/true);
                    return;
                }
            }

            if (not on_serve) {
                // the tree is known locally, so we can upload it to remote CAS
                // for the serve endpoint to retrieve it and set up the root
                if (remote_api == nullptr) {
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
                if (not EnsureAbsentRootOnServe(
                        *serve,
                        tree_id,
                        native_storage_config->GitRoot(), /*repo_root*/
                        native_storage_config,
                        compat_storage_config,
                        local_api,
                        remote_api,
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
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& ws_setter,
    ContentGitMap::LoggerPtr const& logger) {
    if (key.pragma_special) {
        // get the resolved tree
        auto tree_id_file = StorageUtils::GetResolvedTreeIDFile(
            *native_storage_config, tree_hash, *key.pragma_special);
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
                                   serve,
                                   native_storage_config,
                                   compat_storage_config,
                                   local_api,
                                   remote_api,
                                   is_cache_hit,
                                   ws_setter,
                                   logger);
            }
            else {
                (*ws_setter)(
                    std::pair(nlohmann::json::array(
                                  {FileRoot::kGitTreeMarker,
                                   *resolved_tree_id,
                                   native_storage_config->GitRoot().string()}),
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
                [critical_git_op_map,
                 tree_hash,
                 tree_id_file,
                 is_cache_hit,
                 key,
                 is_absent,
                 serve,
                 native_storage_config,
                 compat_storage_config,
                 local_api,
                 remote_api,
                 ts,
                 ws_setter,
                 logger](auto const& hashes) {
                    auto const& resolved_tree_id = hashes[0]->id;
                    // keep tree alive in Git cache via a tagged commit
                    GitOpKey op_key = {
                        .params =
                            {
                                native_storage_config
                                    ->GitRoot(),              // target_path
                                resolved_tree_id,             // git_hash
                                "Keep referenced tree alive"  // message
                            },
                        .op_type = GitOpType::KEEP_TREE};
                    critical_git_op_map->ConsumeAfterKeysReady(
                        ts,
                        {std::move(op_key)},
                        [resolved_tree_id,
                         key,
                         tree_id_file,
                         is_absent,
                         serve,
                         native_storage_config,
                         compat_storage_config,
                         local_api,
                         remote_api,
                         is_cache_hit,
                         ws_setter,
                         logger](auto const& values) {
                            GitOpValue op_result = *values[0];
                            // check flag
                            if (not op_result.result) {
                                (*logger)("Keep tree failed",
                                          /*fatal=*/true);
                                return;
                            }
                            // cache the resolved tree in the CAS map
                            if (not StorageUtils::WriteTreeIDFile(
                                    tree_id_file, resolved_tree_id)) {
                                (*logger)(
                                    fmt::format("Failed to write resolved tree "
                                                "id to file {}",
                                                tree_id_file.string()),
                                    /*fatal=*/true);
                                return;
                            }
                            // set the workspace root
                            if (is_absent) {
                                EnsureRootAsAbsent(resolved_tree_id,
                                                   key,
                                                   serve,
                                                   native_storage_config,
                                                   compat_storage_config,
                                                   local_api,
                                                   remote_api,
                                                   is_cache_hit,
                                                   ws_setter,
                                                   logger);
                            }
                            else {
                                (*ws_setter)(std::pair(
                                    nlohmann::json::array(
                                        {FileRoot::kGitTreeMarker,
                                         resolved_tree_id,
                                         native_storage_config->GitRoot()
                                             .string()}),
                                    /*is_cache_hit=*/is_cache_hit));
                            }
                        },
                        [logger,
                         target_path = native_storage_config->GitRoot()](
                            auto const& msg, bool fatal) {
                            (*logger)(
                                fmt::format("While running critical Git op "
                                            "KEEP_TREE for target {}:\n{}",
                                            target_path.string(),
                                            msg),
                                fatal);
                        });
                },
                [logger, hash = key.archive.content_hash.Hash()](
                    auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While resolving symlinks for "
                                          "content {}:\n{}",
                                          hash,
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
                               serve,
                               native_storage_config,
                               compat_storage_config,
                               local_api,
                               remote_api,
                               is_cache_hit,
                               ws_setter,
                               logger);
        }
        else {
            (*ws_setter)(
                std::pair(nlohmann::json::array(
                              {FileRoot::kGitTreeMarker,
                               tree_hash,
                               native_storage_config->GitRoot().string()}),
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
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
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
                       serve,
                       native_storage_config,
                       compat_storage_config,
                       local_api,
                       remote_api,
                       critical_git_op_map,
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
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    // extract archive
    auto tmp_dir = native_storage_config->CreateTypedTmpDir(key.repo_type);
    if (not tmp_dir) {
        (*logger)(fmt::format("Failed to create tmp path for {} target {}",
                              key.repo_type,
                              key.archive.content_hash.Hash()),
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
    CommitInfo c_info{
        tmp_dir->GetPath(), key.repo_type, key.archive.content_hash.Hash()};
    import_to_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(c_info)},
        [tmp_dir,  // keep tmp_dir alive
         archive_tree_id_file,
         key,
         is_absent,
         serve,
         native_storage_config,
         compat_storage_config,
         local_api,
         remote_api,
         critical_git_op_map,
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
                                    serve,
                                    native_storage_config,
                                    compat_storage_config,
                                    local_api,
                                    remote_api,
                                    critical_git_op_map,
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

auto IdFileExistsInOlderGeneration(
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    ArchiveRepoInfo const& key) -> std::optional<std::size_t> {
    for (std::size_t generation = 1;
         generation < native_storage_config->num_generations;
         generation++) {
        auto archive_tree_id_file =
            StorageUtils::GetArchiveTreeIDFile(*native_storage_config,
                                               key.repo_type,
                                               key.archive.content_hash.Hash(),
                                               generation);
        if (FileSystemManager::Exists(archive_tree_id_file)) {
            return generation;
        }
    }
    return std::nullopt;
}

void HandleLocallyKnownTree(
    ArchiveRepoInfo const& key,
    std::filesystem::path const& archive_tree_id_file,
    bool fetch_absent,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    // read archive_tree_id from file tree_id_file
    auto archive_tree_id = FileSystemManager::ReadFile(archive_tree_id_file);
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
                               native_storage_config->GitRoot(),  // target_path
                               "",                                // git_hash
                               std::nullopt,                      // message
                               std::nullopt,                      // source_path
                               true                               // init_bare
                           },
                       .op_type = GitOpType::ENSURE_INIT};
    critical_git_op_map->ConsumeAfterKeysReady(
        ts,
        {std::move(op_key)},
        [archive_tree_id = *archive_tree_id,
         key,
         fetch_absent,
         serve,
         native_storage_config,
         compat_storage_config,
         local_api,
         remote_api,
         critical_git_op_map,
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
            auto wrapped_logger = std::make_shared<AsyncMapConsumerLogger>(
                [&logger](auto const& msg, bool fatal) {
                    (*logger)(fmt::format("While getting subtree from "
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
                serve,
                native_storage_config,
                compat_storage_config,
                local_api,
                remote_api,
                critical_git_op_map,
                resolve_symlinks_map,
                ts,
                setter,
                logger);
        },
        [logger, target_path = native_storage_config->GitRoot()](
            auto const& msg, bool fatal) {
            (*logger)(fmt::format("While running critical Git "
                                  "op ENSURE_INIT for "
                                  "target {}:\n{}",
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}

void HandleKnownInOlderGenerationAfterImport(
    ArchiveRepoInfo const& key,
    const std::string& tree_id,
    bool fetch_absent,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    // Now that we have the tree persisted in the git repository of the youngest
    // generation; hence we can write the map-entry.
    auto archive_tree_id_file = StorageUtils::GetArchiveTreeIDFile(
        *native_storage_config, key.repo_type, key.archive.content_hash.Hash());
    if (not StorageUtils::WriteTreeIDFile(archive_tree_id_file, tree_id)) {
        (*logger)(fmt::format("Failed to write tree id to file {}",
                              archive_tree_id_file.string()),
                  /*fatal=*/true);
        return;
    }
    // Now that we also have the the ID-file written, we're in the situation
    // as if we had a cache hit in the first place.
    HandleLocallyKnownTree(key,
                           archive_tree_id_file,
                           fetch_absent,
                           serve,
                           native_storage_config,
                           compat_storage_config,
                           resolve_symlinks_map,
                           critical_git_op_map,
                           local_api,
                           remote_api,
                           ts,
                           setter,
                           logger);
}

void HandleKnownInOlderGenerationAfterTaggingAndInit(
    ArchiveRepoInfo const& key,
    std::string tree_id,
    std::string tag,
    GitCASPtr const& git_cas,
    std::filesystem::path const& source,
    bool fetch_absent,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    auto git_repo = GitRepoRemote::Open(git_cas);
    if (not git_repo) {
        (*logger)("Could not open just initialized repository {}",
                  /*fatal=*/true);
        return;
    }
    auto fetch_logger = std::make_shared<AsyncMapConsumerLogger>(
        [logger, tag, source](auto const& msg, bool fatal) {
            (*logger)(fmt::format("While fetching {} from {}:\n{}",
                                  tag,
                                  source.string(),
                                  msg),
                      fatal);
        });
    if (not git_repo->LocalFetchViaTmpRepo(
            *native_storage_config, source, tag, fetch_logger)) {
        return;
    }
    GitOpKey op_key = {.params =
                           {
                               native_storage_config->GitRoot(),  // target_path
                               tree_id,                           // git_hash
                               "Keep referenced tree alive"       // message
                           },
                       .op_type = GitOpType::KEEP_TREE};
    critical_git_op_map->ConsumeAfterKeysReady(
        ts,
        {std::move(op_key)},
        [key,
         tree_id,
         git_cas,
         fetch_absent,
         serve,
         native_storage_config,
         compat_storage_config,
         resolve_symlinks_map,
         critical_git_op_map,
         local_api,
         remote_api,
         ts,
         setter,
         logger](auto const& values) {
            GitOpValue op_result = *values[0];
            // check flag
            if (not op_result.result) {
                (*logger)("Keep tag failed",
                          /*fatal=*/true);
                return;
            }
            HandleKnownInOlderGenerationAfterImport(key,
                                                    tree_id,
                                                    fetch_absent,
                                                    serve,
                                                    native_storage_config,
                                                    compat_storage_config,
                                                    resolve_symlinks_map,
                                                    critical_git_op_map,
                                                    local_api,
                                                    remote_api,
                                                    ts,
                                                    setter,
                                                    logger);
        },
        [logger, tree_id](auto const& msg, bool fatal) {
            (*logger)(
                fmt::format(
                    "While tagging to keep tree {} alive:\n{}", tree_id, msg),
                fatal);
        });
}

void HandleKnownInOlderGenerationAfterTagging(
    ArchiveRepoInfo const& key,
    const std::string& tree_id,
    const std::string& tag,
    std::filesystem::path const& source,
    bool fetch_absent,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    GitOpKey op_key = {.params =
                           {
                               native_storage_config->GitRoot(),  // target_path
                               "",                                // git_hash
                               std::nullopt,                      // message
                               std::nullopt,                      // source_path
                               true                               // init_bare
                           },
                       .op_type = GitOpType::ENSURE_INIT};
    critical_git_op_map->ConsumeAfterKeysReady(
        ts,
        {std::move(op_key)},
        [key,
         tree_id,
         tag,
         source,
         fetch_absent,
         serve,
         native_storage_config,
         compat_storage_config,
         resolve_symlinks_map,
         critical_git_op_map,
         local_api,
         remote_api,
         ts,
         setter,
         logger](auto const& values) {
            GitOpValue op_result = *values[0];
            if (not op_result.result) {
                (*logger)("Git init failed",
                          /*fatal=*/true);
                return;
            }
            HandleKnownInOlderGenerationAfterTaggingAndInit(
                key,
                tree_id,
                tag,
                op_result.git_cas,
                source,
                fetch_absent,
                serve,
                native_storage_config,
                compat_storage_config,
                resolve_symlinks_map,
                critical_git_op_map,
                local_api,
                remote_api,
                ts,
                setter,
                logger);
        },
        [logger, target_path = native_storage_config->GitRoot()](
            auto const& msg, bool fatal) {
            (*logger)(fmt::format("While running critical Git op "
                                  "ENSURE_INIT for target {}:\n{}",
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}

void HandleKnownInOlderGeneration(
    ArchiveRepoInfo const& key,
    std::size_t generation,
    bool fetch_absent,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& setter,
    ContentGitMap::LoggerPtr const& logger) {
    auto archive_tree_id_file =
        StorageUtils::GetArchiveTreeIDFile(*native_storage_config,
                                           key.repo_type,
                                           key.archive.content_hash.Hash(),
                                           generation);
    auto archive_tree_id = FileSystemManager::ReadFile(archive_tree_id_file);
    if (not archive_tree_id) {
        (*logger)(fmt::format("Failed to read tree id from file {}",
                              archive_tree_id_file.string()),
                  /*fatal=*/true);
        return;
    }
    auto source = native_storage_config->GitGenerationRoot(generation);

    GitOpKey op_key = {.params =
                           {
                               source,                    // target_path
                               *archive_tree_id,          // git_hash
                               "Tag commit for fetching"  // message
                           },
                       .op_type = GitOpType::KEEP_TREE};
    critical_git_op_map->ConsumeAfterKeysReady(
        ts,
        {std::move(op_key)},
        [key,
         tree_id = *archive_tree_id,
         source,
         fetch_absent,
         serve,
         native_storage_config,
         compat_storage_config,
         resolve_symlinks_map,
         critical_git_op_map,
         local_api,
         remote_api,
         ts,
         setter,
         logger](auto const& values) {
            GitOpValue op_result = *values[0];
            if (not op_result.result) {
                (*logger)("Keep tag failed", /*fatal=*/true);
                return;
            }
            auto tag = *op_result.result;
            HandleKnownInOlderGenerationAfterTagging(key,
                                                     tree_id,
                                                     tag,
                                                     source,
                                                     fetch_absent,
                                                     serve,
                                                     native_storage_config,
                                                     compat_storage_config,
                                                     resolve_symlinks_map,
                                                     critical_git_op_map,
                                                     local_api,
                                                     remote_api,
                                                     ts,
                                                     setter,
                                                     logger);
        },
        [logger, source, hash = *archive_tree_id](auto const& msg, bool fatal) {
            (*logger)(
                fmt::format("While tagging tree {} in {} for fetching:\n{}",
                            source.string(),
                            hash,
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
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<Storage const*> const& native_storage,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    bool fetch_absent,
    gsl::not_null<JustMRProgress*> const& progress,
    std::size_t jobs) -> ContentGitMap {
    auto gitify_content = [content_cas_map,
                           import_to_git_map,
                           resolve_symlinks_map,
                           critical_git_op_map,
                           just_mr_paths,
                           additional_mirrors,
                           ca_info,
                           serve,
                           native_storage_config,
                           compat_storage_config,
                           native_storage,
                           local_api,
                           remote_api,
                           fetch_absent,
                           progress](auto ts,
                                     auto setter,
                                     auto logger,
                                     auto /* unused */,
                                     auto const& key) {
        auto archive_tree_id_file =
            StorageUtils::GetArchiveTreeIDFile(*native_storage_config,
                                               key.repo_type,
                                               key.archive.content_hash.Hash());
        if (FileSystemManager::Exists(archive_tree_id_file)) {
            HandleLocallyKnownTree(key,
                                   archive_tree_id_file,
                                   fetch_absent,
                                   serve,
                                   native_storage_config,
                                   compat_storage_config,
                                   resolve_symlinks_map,
                                   critical_git_op_map,
                                   local_api,
                                   remote_api,
                                   ts,
                                   setter,
                                   logger);
        }
        else if (auto generation = IdFileExistsInOlderGeneration(
                     native_storage_config, key)) {
            HandleKnownInOlderGeneration(key,
                                         *generation,
                                         fetch_absent,
                                         serve,
                                         native_storage_config,
                                         compat_storage_config,
                                         resolve_symlinks_map,
                                         critical_git_op_map,
                                         local_api,
                                         remote_api,
                                         ts,
                                         setter,
                                         logger);
        }
        else {
            // separate logic between absent and present roots
            if (key.absent and not fetch_absent) {
                // request the resolved subdir tree from the serve endpoint, if
                // given
                if (serve != nullptr) {
                    auto const serve_result = serve->RetrieveTreeFromArchive(
                        key.archive.content_hash.Hash(),
                        key.repo_type,
                        key.subdir,
                        key.pragma_special,
                        /*sync_tree = */ false);
                    if (serve_result) {
                        // set the workspace root as absent
                        progress->TaskTracker().Stop(key.archive.origin);
                        (*setter)(std::pair(
                            nlohmann::json::array(
                                {FileRoot::kGitTreeMarker, serve_result->tree}),
                            /*is_cache_hit = */ false));
                        return;
                    }
                    // check if serve failure was due to archive content
                    // not being found or it is otherwise fatal
                    if (serve_result.error() == GitLookupError::Fatal) {
                        (*logger)(
                            fmt::format("Serve endpoint failed to set up root "
                                        "from known archive content {}",
                                        key.archive.content_hash.Hash()),
                            /*fatal=*/true);
                        return;
                    }
                }
                // if serve endpoint cannot set up the root, we might still be
                // able to set up the absent root with local information, and if
                // a serve endpoint exists we can upload it the root ourselves;

                // check if content already in CAS
                auto const& native_cas = native_storage->CAS();
                auto const digest = ArtifactDigest{key.archive.content_hash, 0};
                if (auto content_cas_path =
                        native_cas.BlobPath(digest, /*is_executable=*/false)) {
                    ExtractAndImportToGit(key,
                                          *content_cas_path,
                                          archive_tree_id_file,
                                          /*is_absent = */ true,
                                          serve,
                                          native_storage_config,
                                          compat_storage_config,
                                          local_api,
                                          remote_api,
                                          critical_git_op_map,
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
                            native_storage_config->GitRoot(),  // target_path
                            "",                                // git_hash
                            std::nullopt,                      // message
                            std::nullopt,                      // source_path
                            true                               // init_bare
                        },
                    .op_type = GitOpType::ENSURE_INIT};
                critical_git_op_map->ConsumeAfterKeysReady(
                    ts,
                    {std::move(op_key)},
                    [key,
                     digest,
                     archive_tree_id_file,
                     critical_git_op_map,
                     import_to_git_map,
                     resolve_symlinks_map,
                     just_mr_paths,
                     additional_mirrors,
                     ca_info,
                     serve,
                     native_storage_config,
                     compat_storage_config,
                     native_storage,
                     local_api,
                     remote_api,
                     progress,
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
                                [&logger,
                                 hash = key.archive.content_hash.Hash()](
                                    auto const& msg, bool fatal) {
                                    (*logger)(
                                        fmt::format("While verifying presence "
                                                    "of blob {}:\n{}",
                                                    hash,
                                                    msg),
                                        fatal);
                                });
                        auto res = just_git_repo->TryReadBlob(
                            key.archive.content_hash.Hash(), wrapped_logger);
                        if (not res.first) {
                            // blob check failed
                            return;
                        }
                        auto const& native_cas = native_storage->CAS();
                        if (res.second) {
                            // blob found; add it to CAS
                            if (not native_cas.StoreBlob(
                                    *res.second,
                                    /*is_executable=*/false)) {
                                (*logger)(fmt::format(
                                              "Failed to store content "
                                              "{} to local CAS",
                                              key.archive.content_hash.Hash()),
                                          /*fatal=*/true);
                                return;
                            }
                            if (auto content_cas_path = native_cas.BlobPath(
                                    digest, /*is_executable=*/false)) {
                                ExtractAndImportToGit(key,
                                                      *content_cas_path,
                                                      archive_tree_id_file,
                                                      /*is_absent=*/true,
                                                      serve,
                                                      native_storage_config,
                                                      compat_storage_config,
                                                      local_api,
                                                      remote_api,
                                                      critical_git_op_map,
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
                        progress->TaskTracker().Start(key.archive.origin);
                        // add distfile to CAS
                        auto repo_distfile =
                            (key.archive.distfile
                                 ? key.archive.distfile.value()
                                 : std::filesystem::path(key.archive.fetch_url)
                                       .filename()
                                       .string());
                        StorageUtils::AddDistfileToCAS(
                            *native_storage, repo_distfile, just_mr_paths);
                        // check if content is in CAS now
                        if (auto content_cas_path = native_cas.BlobPath(
                                digest, /*is_executable=*/false)) {
                            progress->TaskTracker().Stop(key.archive.origin);
                            ExtractAndImportToGit(key,
                                                  *content_cas_path,
                                                  archive_tree_id_file,
                                                  /*is_absent=*/true,
                                                  serve,
                                                  native_storage_config,
                                                  compat_storage_config,
                                                  local_api,
                                                  remote_api,
                                                  critical_git_op_map,
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
                                              key.archive.content_hash.Hash()),
                                  /*fatal=*/true);
                    },
                    [logger, target_path = native_storage_config->GitRoot()](
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
                     native_storage_config,
                     native_storage,
                     critical_git_op_map,
                     import_to_git_map,
                     resolve_symlinks_map,
                     ts,
                     setter,
                     logger]([[maybe_unused]] auto const& values) {
                        // content is in local CAS now
                        auto const& native_cas = native_storage->CAS();
                        auto content_cas_path =
                            native_cas
                                .BlobPath(
                                    ArtifactDigest{key.archive.content_hash, 0},
                                    /*is_executable=*/false)
                                .value();
                        // root can only be present, so default all arguments
                        // that refer to a serve endpoint
                        ExtractAndImportToGit(key,
                                              content_cas_path,
                                              archive_tree_id_file,
                                              /*is_absent=*/false,
                                              /*serve=*/nullptr,
                                              native_storage_config,
                                              /*compat_storage_config=*/nullptr,
                                              /*local_api=*/nullptr,
                                              /*remote_api=*/nullptr,
                                              critical_git_op_map,
                                              import_to_git_map,
                                              resolve_symlinks_map,
                                              ts,
                                              setter,
                                              logger);
                    },
                    [logger, hash = key.archive.content_hash.Hash()](
                        auto const& msg, bool fatal) {
                        (*logger)(fmt::format("While ensuring content {} is in "
                                              "CAS:\n{}",
                                              hash,
                                              msg),
                                  fatal);
                    });
            }
        }
    };
    return AsyncMapConsumer<ArchiveRepoInfo, std::pair<nlohmann::json, bool>>(
        gitify_content, jobs);
}
