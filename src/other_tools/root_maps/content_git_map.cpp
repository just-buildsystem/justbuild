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

#include "src/buildtool/execution_api/local/file_storage.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/utils/cpp/archive_ops.hpp"

namespace {

/// \brief Extracts the archive of given type into the destination directory
/// provided. Returns nullopt on success, or error string on failure.
[[nodiscard]] auto ExtractArchive(std::filesystem::path const& archive,
                                  std::string const& repo_type,
                                  std::filesystem::path const& dst_dir) noexcept
    -> std::optional<std::string> {
    if (repo_type == "archive") {
        return ArchiveOps::ExtractArchive(
            ArchiveType::kArchiveTypeTarGz, archive, dst_dir);
    }
    if (repo_type == "zip") {
        return ArchiveOps::ExtractArchive(
            ArchiveType::kArchiveTypeZip, archive, dst_dir);
    }
    return "unrecognized repository type";
}

}  // namespace

auto CreateContentGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::size_t jobs) -> ContentGitMap {
    auto gitify_content = [content_cas_map,
                           import_to_git_map,
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
            GitOpKey op_key = {
                {
                    JustMR::Utils::GetGitCacheRoot(),  // target_path
                    "",                                // git_hash
                    "",                                // branch
                    std::nullopt,                      // message
                    true                               // init_bare
                },
                GitOpType::ENSURE_INIT};
            critical_git_op_map->ConsumeAfterKeysReady(
                ts,
                {std::move(op_key)},
                [archive_tree_id = *archive_tree_id,
                 subdir = key.subdir,
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
                    auto just_git_repo = GitRepo::Open(op_result.git_cas);
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
                    // get subtree id and return workspace root
                    auto subtree_hash = just_git_repo->GetSubtreeFromTree(
                        archive_tree_id, subdir, wrapped_logger);
                    if (not subtree_hash) {
                        return;
                    }
                    // set the workspace root
                    (*setter)(nlohmann::json::array(
                        {"git tree",
                         *subtree_hash,
                         JustMR::Utils::GetGitCacheRoot().string()}));
                },
                [logger, target_path = JustMR::Utils::GetGitCacheRoot()](
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
                 import_to_git_map,
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
                    auto const& casf = LocalCAS<ObjectType::File>::Instance();
                    // content is in CAS if here, so no need to check nullopt
                    auto content_cas_path =
                        casf.BlobPath(ArtifactDigest(content_id, 0, false))
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
                         subdir,
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
                                GitCAS::Open(JustMR::Utils::GetGitCacheRoot());
                            if (not just_git_cas) {
                                (*logger)(
                                    "Could not open Git cache object database!",
                                    /*fatal=*/true);
                                return;
                            }
                            // fetch all into Git cache
                            auto just_git_repo = GitRepo::Open(just_git_cas);
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
                            // get subtree id and return workspace root
                            auto subtree_hash =
                                just_git_repo->GetSubtreeFromTree(
                                    archive_tree_id, subdir, wrapped_logger);
                            if (not subtree_hash) {
                                return;
                            }
                            // set the workspace root
                            (*setter)(nlohmann::json::array(
                                {"git tree",
                                 *subtree_hash,
                                 JustMR::Utils::GetGitCacheRoot().string()}));
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
    return AsyncMapConsumer<ArchiveRepoInfo, nlohmann::json>(gitify_content,
                                                             jobs);
}
