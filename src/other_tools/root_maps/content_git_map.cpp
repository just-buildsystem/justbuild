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

#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
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
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<TaskSystem*> const& ts,
    ContentGitMap::SetterPtr const& ws_setter,
    ContentGitMap::LoggerPtr const& logger) {
    if (pragma_special) {
        // get the resolved tree
        auto tree_id_file =
            JustMR::Utils::GetResolvedTreeIDFile(tree_hash, *pragma_special);
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
            if (not absent) {
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
                    if (not JustMR::Utils::WriteTreeIDFile(tree_id_file,
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
                    if (not absent) {
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
        if (not absent) {
            root.emplace_back(StorageConfig::GitRoot().string());
        }
        (*ws_setter)(std::pair(std::move(root), is_cache_hit));
    }
}

}  // namespace

auto CreateContentGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<ResolveSymlinksMap*> const& resolve_symlinks_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::size_t jobs) -> ContentGitMap {
    auto gitify_content = [content_cas_map,
                           import_to_git_map,
                           resolve_symlinks_map,
                           critical_git_op_map](auto ts,
                                                auto setter,
                                                auto logger,
                                                auto /* unused */,
                                                auto const& key) {
        auto archive_tree_id_file = JustMR::Utils::GetArchiveTreeIDFile(
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
            // do the fetch and import_to_git
            content_cas_map->ConsumeAfterKeysReady(
                ts,
                {key.archive},
                [archive_tree_id_file,
                 repo_type = key.repo_type,
                 content_id = key.archive.content,
                 subdir = key.subdir,
                 pragma_special = key.pragma_special,
                 absent = key.absent,
                 import_to_git_map,
                 resolve_symlinks_map,
                 ts,
                 setter,
                 logger]([[maybe_unused]] auto const& values) {
                    // content is in CAS
                    // extract archive
                    auto tmp_dir = JustMR::Utils::CreateTypedTmpDir(repo_type);
                    if (not tmp_dir) {
                        (*logger)(fmt::format("Failed to create tmp path for "
                                              "{} target {}",
                                              repo_type,
                                              content_id),
                                  /*fatal=*/true);
                        return;
                    }
                    auto const& cas = Storage::Instance().CAS();
                    // content is in CAS if here, so no need to check nullopt
                    auto content_cas_path =
                        cas.BlobPath(ArtifactDigest(content_id, 0, false),
                                     /*is_executable=*/false)
                            .value();
                    auto res = ExtractArchive(
                        content_cas_path, repo_type, tmp_dir->GetPath());
                    if (res != std::nullopt) {
                        (*logger)(fmt::format("Failed to extract archive {} "
                                              "from CAS with error: {}",
                                              content_cas_path.string(),
                                              *res),
                                  /*fatal=*/true);
                        return;
                    }
                    // import to git
                    CommitInfo c_info{
                        tmp_dir->GetPath(), repo_type, content_id};
                    import_to_git_map->ConsumeAfterKeysReady(
                        ts,
                        {std::move(c_info)},
                        [tmp_dir,  // keep tmp_dir alive
                         archive_tree_id_file,
                         content_id,
                         subdir,
                         pragma_special,
                         absent,
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
                            // write to tree id file
                            if (not JustMR::Utils::WriteTreeIDFile(
                                    archive_tree_id_file, archive_tree_id)) {
                                (*logger)(
                                    fmt::format("Failed to write tree id "
                                                "to file {}",
                                                archive_tree_id_file.string()),
                                    /*fatal=*/true);
                                return;
                            }
                            // we look for subtree in Git cache
                            auto just_git_cas =
                                GitCAS::Open(StorageConfig::GitRoot());
                            if (not just_git_cas) {
                                (*logger)(
                                    "Could not open Git cache object database!",
                                    /*fatal=*/true);
                                return;
                            }
                            auto just_git_repo =
                                GitRepoRemote::Open(just_git_cas);
                            if (not just_git_repo) {
                                (*logger)(
                                    "Could not open Git cache repository!",
                                    /*fatal=*/true);
                                return;
                            }
                            // setup wrapped logger
                            auto wrapped_logger =
                                std::make_shared<AsyncMapConsumerLogger>(
                                    [&logger](auto const& msg, bool fatal) {
                                        (*logger)(
                                            fmt::format("While getting subtree "
                                                        "from tree:\n{}",
                                                        msg),
                                            fatal);
                                    });
                            // get subtree id
                            auto subtree_hash =
                                just_git_repo->GetSubtreeFromTree(
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
                                               resolve_symlinks_map,
                                               ts,
                                               setter,
                                               logger);
                        },
                        [logger, target_path = tmp_dir->GetPath()](
                            auto const& msg, bool fatal) {
                            (*logger)(fmt::format("While importing target {} "
                                                  "to Git:\n{}",
                                                  target_path.string(),
                                                  msg),
                                      fatal);
                        });
                },
                [logger, content = key.archive.content](auto const& msg,
                                                        bool fatal) {
                    (*logger)(
                        fmt::format("While ensuring content {} is in CAS:\n{}",
                                    content,
                                    msg),
                        fatal);
                });
        }
    };
    return AsyncMapConsumer<ArchiveRepoInfo, std::pair<nlohmann::json, bool>>(
        gitify_content, jobs);
}
