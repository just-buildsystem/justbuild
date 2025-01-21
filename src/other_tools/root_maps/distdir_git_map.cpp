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

#include "src/other_tools/root_maps/distdir_git_map.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>

#include "fmt/core.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/git_types.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/other_tools/git_operations/git_ops_types.hpp"
#include "src/other_tools/root_maps/root_utils.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {

/// \brief Create links from CAS content to distdir tmp directory
[[nodiscard]] auto LinkToCAS(
    Storage const& storage,
    std::shared_ptr<std::unordered_map<std::string, std::string>> const&
        content_list,
    std::filesystem::path const& tmp_dir) noexcept -> bool {
    auto const& cas = storage.CAS();
    return std::all_of(
        content_list->begin(),
        content_list->end(),
        [&cas, tmp_dir](auto const& kv) {
            auto const digest =
                ArtifactDigestFactory::Create(cas.GetHashFunction().GetType(),
                                              kv.second,
                                              0,
                                              /*is_tree=*/false);
            if (not digest) {
                return false;
            }
            auto content_path = cas.BlobPath(*digest,
                                             /*is_executable=*/false);
            if (content_path) {
                return FileSystemManager::CreateFileHardlink(
                           *content_path,       // from: cas_path/content_id
                           tmp_dir / kv.first)  // to: tmp_dir/name
                    .has_value();
            }
            return false;
        });
}

/// \brief Called once we know we have the content blobs in local CAS in order
/// to do the import-to-git step. Then it also sets the root.
/// It guarantees the logger is called exactly once with fatal on failure, and
/// the setter on success.
void ImportFromCASAndSetRoot(
    DistdirInfo const& key,
    StorageConfig const& native_storage_config,
    Storage const& native_storage,
    std::filesystem::path const& distdir_tree_id_file,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<TaskSystem*> const& ts,
    DistdirGitMap::SetterPtr const& setter,
    DistdirGitMap::LoggerPtr const& logger) {
    // create the links to CAS
    auto tmp_dir = native_storage_config.CreateTypedTmpDir("distdir");
    if (not tmp_dir) {
        (*logger)(fmt::format("Failed to create tmp path for "
                              "distdir target {}",
                              key.content_id),
                  /*fatal=*/true);
        return;
    }
    // link content from CAS into tmp dir
    if (not LinkToCAS(native_storage, key.content_list, tmp_dir->GetPath())) {
        (*logger)(fmt::format("Failed to create links to CAS content!",
                              key.content_id),
                  /*fatal=*/true);
        return;
    }
    // do import to git
    CommitInfo c_info{tmp_dir->GetPath(), "distdir", key.content_id};
    import_to_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(c_info)},
        [tmp_dir,  // keep tmp_dir alive
         distdir_tree_id_file,
         git_root = native_storage_config.GitRoot().string(),
         setter,
         logger](auto const& values) {
            // check for errors
            if (not values[0]->second) {
                (*logger)("Importing to git failed",
                          /*fatal=*/true);
                return;
            }
            // only the tree is of interest
            std::string distdir_tree_id = values[0]->first;
            // write to tree id file
            if (not StorageUtils::WriteTreeIDFile(distdir_tree_id_file,
                                                  distdir_tree_id)) {
                (*logger)(fmt::format("Failed to write tree id to file {}",
                                      distdir_tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            // set the workspace root as present
            (*setter)(std::pair(
                nlohmann::json::array(
                    {FileRoot::kGitTreeMarker, distdir_tree_id, git_root}),
                /*is_cache_hit=*/false));
        },
        [logger, target_path = tmp_dir->GetPath()](auto const& msg,
                                                   bool fatal) {
            (*logger)(fmt::format("While importing target {} to git:\n{}",
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}

}  // namespace

auto CreateDistdirGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    ServeApi const* serve,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    gsl::not_null<Storage const*> const& native_storage,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    std::size_t jobs) -> DistdirGitMap {
    auto distdir_to_git = [content_cas_map,
                           import_to_git_map,
                           critical_git_op_map,
                           serve,
                           native_storage_config,
                           compat_storage_config,
                           native_storage,
                           local_api,
                           remote_api](auto ts,
                                       auto setter,
                                       auto logger,
                                       auto /* unused */,
                                       auto const& key) {
        auto distdir_tree_id_file = StorageUtils::GetDistdirTreeIDFile(
            *native_storage_config, key.content_id);
        if (FileSystemManager::Exists(distdir_tree_id_file)) {
            // read distdir_tree_id from file tree_id_file
            auto distdir_tree_id =
                FileSystemManager::ReadFile(distdir_tree_id_file);
            if (not distdir_tree_id) {
                (*logger)(fmt::format("Failed to read tree id from file {}",
                                      distdir_tree_id_file.string()),
                          /*fatal=*/true);
                return;
            }
            // ensure Git cache
            // define Git operation to be done
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
                [distdir_tree_id = *distdir_tree_id,
                 content_id = key.content_id,
                 key,
                 serve,
                 native_storage_config,
                 compat_storage_config,
                 local_api,
                 remote_api,
                 setter,
                 logger](auto const& values) {
                    GitOpValue op_result = *values[0];
                    // check flag
                    if (not op_result.result) {
                        (*logger)("Git init failed",
                                  /*fatal=*/true);
                        return;
                    }
                    // subdir is "." here, so no need to deal with the Git cache
                    // and we can simply set the workspace root
                    if (key.absent) {
                        if (serve != nullptr) {
                            // check if serve endpoint has this root
                            auto has_tree = CheckServeHasAbsentRoot(
                                *serve, distdir_tree_id, logger);
                            if (not has_tree) {
                                return;
                            }
                            if (not *has_tree) {
                                // try to see if serve endpoint has the
                                // information to prepare the root itself
                                auto const serve_result =
                                    serve->RetrieveTreeFromDistdir(
                                        key.content_list,
                                        /*sync_tree=*/false);
                                if (serve_result) {
                                    // if serve has set up the tree, it must
                                    // match what we expect
                                    if (distdir_tree_id != serve_result->tree) {
                                        (*logger)(
                                            fmt::format(
                                                "Mismatch in served root tree "
                                                "id:\nexpected {}, but got {}",
                                                distdir_tree_id,
                                                serve_result->tree),
                                            /*fatal=*/true);
                                        return;
                                    }
                                }
                                else {
                                    // check if serve failure was due to distdir
                                    // content not being found or it is
                                    // otherwise fatal
                                    if (serve_result.error() ==
                                        GitLookupError::Fatal) {
                                        (*logger)(
                                            fmt::format(
                                                "Serve endpoint failed to set "
                                                "up root from known distdir "
                                                "content {}",
                                                content_id),
                                            /*fatal=*/true);
                                        return;
                                    }
                                    if (remote_api == nullptr) {
                                        (*logger)(
                                            fmt::format(
                                                "Missing or incompatible "
                                                "remote-execution endpoint "
                                                "needed to sync workspace root "
                                                "{} with the serve endpoint.",
                                                distdir_tree_id),
                                            /*fatal=*/true);
                                        return;
                                    }
                                    // the tree is known locally, so we upload
                                    // it to remote CAS for the serve endpoint
                                    // to retrieve it and set up the root
                                    if (not EnsureAbsentRootOnServe(
                                            *serve,
                                            distdir_tree_id,
                                            native_storage_config
                                                ->GitRoot(), /*repo_root*/
                                            native_storage_config,
                                            compat_storage_config,
                                            &*local_api,
                                            remote_api,
                                            logger,
                                            true /*no_sync_is_fatal*/)) {
                                        return;
                                    }
                                }
                            }
                        }
                        else {
                            // give warning
                            (*logger)(
                                fmt::format("Workspace root {} marked absent "
                                            "but no serve endpoint provided.",
                                            distdir_tree_id),
                                /*fatal=*/false);
                        }
                        // set root as absent
                        (*setter)(std::pair(
                            nlohmann::json::array(
                                {FileRoot::kGitTreeMarker, distdir_tree_id}),
                            /*is_cache_hit=*/true));
                    }
                    else {
                        // set root as present
                        (*setter)(std::pair(
                            nlohmann::json::array(
                                {FileRoot::kGitTreeMarker,
                                 distdir_tree_id,
                                 native_storage_config->GitRoot().string()}),
                            /*is_cache_hit=*/true));
                    }
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
        // if no association file exists
        else {
            // create in-memory Git tree of distdir content to get the tree id
            GitRepo::tree_entries_t entries{};
            entries.reserve(key.content_list->size());
            for (auto const& kv : *key.content_list) {
                // tree_entries_t type expects raw ids
                auto raw_id = FromHexString(kv.second);
                if (not raw_id) {
                    (*logger)(fmt::format("While processing distdir {}: "
                                          "Unexpected failure in conversion to "
                                          "raw id of distfile content {}",
                                          key.content_id,
                                          kv.second),
                              /*is_fatal=*/true);
                    return;
                }
                entries[*raw_id].emplace_back(kv.first, ObjectType::File);
            }
            auto tree = GitRepo::CreateShallowTree(entries);
            if (not tree) {
                (*logger)(fmt::format("Failed to construct in-memory tree for "
                                      "distdir content {}",
                                      key.content_id),
                          /*is_fatal=*/true);
                return;
            }
            // get hash from raw_id
            auto const tree_id = ToHexString(tree->first);
            // get digest object
            auto const digest = ArtifactDigestFactory::Create(
                HashFunction::Type::GitSHA1, tree_id, 0, /*is_tree=*/true);

            // use this knowledge of the resulting tree identifier to try to set
            // up the absent root without actually checking the local status of
            // each content blob individually
            if (key.absent) {
                if (serve != nullptr) {
                    // first check if serve endpoint has tree
                    auto has_tree =
                        CheckServeHasAbsentRoot(*serve, tree_id, logger);
                    if (not has_tree) {
                        return;
                    }
                    if (*has_tree) {
                        // set workspace root as absent
                        (*setter)(
                            std::pair(nlohmann::json::array(
                                          {FileRoot::kGitTreeMarker, tree_id}),
                                      /*is_cache_hit=*/false));
                        return;
                    }
                    // try to see if serve endpoint has the information to
                    // prepare the root itself
                    auto const serve_result =
                        serve->RetrieveTreeFromDistdir(key.content_list,
                                                       /*sync_tree=*/false);
                    if (serve_result) {
                        // if serve has set up the tree, it must match what we
                        // expect
                        if (tree_id != serve_result->tree) {
                            (*logger)(
                                fmt::format("Mismatch in served root tree "
                                            "id:\nexpected {}, but got {}",
                                            tree_id,
                                            serve_result->tree),
                                /*fatal=*/true);
                            return;
                        }
                        // set workspace root as absent
                        (*setter)(
                            std::pair(nlohmann::json::array(
                                          {FileRoot::kGitTreeMarker, tree_id}),
                                      /*is_cache_hit=*/false));
                        return;
                    }
                    // check if serve failure was due to distdir content not
                    // being found or it is otherwise fatal
                    if (serve_result.error() == GitLookupError::Fatal) {
                        (*logger)(
                            fmt::format("Serve endpoint failed to set up root "
                                        "from known distdir content {}",
                                        key.content_id),
                            /*fatal=*/true);
                        return;
                    }
                    // we cannot continue without a suitable remote set up
                    if (remote_api == nullptr) {
                        (*logger)(fmt::format(
                                      "Cannot create workspace root {} as "
                                      "absent for the provided serve endpoint.",
                                      tree_id),
                                  /*fatal=*/true);
                        return;
                    }
                    // try to supply the serve endpoint with the tree via the
                    // remote CAS
                    if (digest and remote_api->IsAvailable({*digest})) {
                        // tell serve to set up the root from the remote CAS
                        // tree
                        if (serve->GetTreeFromRemote(*digest)) {
                            // set workspace root as absent
                            (*setter)(std::pair(
                                nlohmann::json::array(
                                    {FileRoot::kGitTreeMarker, tree_id}),
                                /*is_cache_hit=*/false));
                            return;
                        }
                        (*logger)(fmt::format("Serve endpoint failed to create "
                                              "workspace root {} that locally "
                                              "was marked absent.",
                                              tree_id),
                                  /*fatal=*/true);
                        return;
                    }
                    // check if we have the tree in local CAS; if yes, upload it
                    // to remote for the serve endpoint to find it
                    if (digest and local_api->IsAvailable({*digest})) {
                        if (not local_api->RetrieveToCas(
                                {Artifact::ObjectInfo{
                                    .digest = *digest,
                                    .type = ObjectType::Tree}},
                                *remote_api)) {
                            (*logger)(fmt::format("Failed to sync tree {} from "
                                                  "local CAS with remote CAS.",
                                                  tree_id),
                                      /*fatal=*/true);
                            return;
                        }
                        // tell serve to set up the root from the remote CAS
                        // tree
                        if (serve->GetTreeFromRemote(*digest)) {
                            // set workspace root as absent
                            (*setter)(std::pair(
                                nlohmann::json::array(
                                    {FileRoot::kGitTreeMarker, tree_id}),
                                /*is_cache_hit=*/false));
                            return;
                        }
                    }
                    // cannot create absent root with given information
                    (*logger)(
                        fmt::format("Serve endpoint failed to create workspace "
                                    "root {} that locally was marked absent.",
                                    tree_id),
                        /*fatal=*/true);
                    return;
                }
                // give warning
                (*logger)(fmt::format("Workspace root {} marked absent but no "
                                      "serve endpoint provided.",
                                      tree_id),
                          /*fatal=*/false);
                // set workspace root as absent
                (*setter)(std::pair(
                    nlohmann::json::array({FileRoot::kGitTreeMarker, tree_id}),
                    false /*no cache hit*/));
                return;
            }

            // if the root is not-absent, the order of checks is different;
            // first, look in the local CAS
            if (digest and local_api->IsAvailable({*digest})) {
                ImportFromCASAndSetRoot(key,
                                        *native_storage_config,
                                        *native_storage,
                                        distdir_tree_id_file,
                                        import_to_git_map,
                                        ts,
                                        setter,
                                        logger);
                // done
                return;
            }
            // now ask serve endpoint if it can set up the root; as this is for
            // a present root, a corresponding remote endpoint is needed
            if (serve != nullptr and remote_api != nullptr) {
                auto const serve_result =
                    serve->RetrieveTreeFromDistdir(key.content_list,
                                                   /*sync_tree=*/true);
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
                    // we only need the serve endpoint to try to set up the
                    // root, as we will check the remote CAS for the resulting
                    // tree anyway
                }
                else {
                    // check if serve failure was due to distdir content not
                    // being found or it is otherwise fatal
                    if (serve_result.error() == GitLookupError::Fatal) {
                        (*logger)(
                            fmt::format("Serve endpoint failed to set up root "
                                        "from known distdir content {}",
                                        key.content_id),
                            /*fatal=*/true);
                        return;
                    }
                }
            }

            // we could not set the root as present using the CAS tree
            // invariant, so now we need to ensure we have all individual blobs
            content_cas_map->ConsumeAfterKeysReady(
                ts,
                *key.repos_to_fetch,
                [distdir_tree_id_file,
                 key,
                 import_to_git_map,
                 native_storage_config,
                 native_storage,
                 ts,
                 setter,
                 logger]([[maybe_unused]] auto const& values) {
                    // archive blobs are in CAS
                    ImportFromCASAndSetRoot(key,
                                            *native_storage_config,
                                            *native_storage,
                                            distdir_tree_id_file,
                                            import_to_git_map,
                                            ts,
                                            setter,
                                            logger);
                },
                [logger, content_id = key.content_id](auto const& msg,
                                                      bool fatal) {
                    (*logger)(fmt::format("While fetching archives for distdir "
                                          "content {}:\n{}",
                                          content_id,
                                          msg),
                              fatal);
                });
        }
    };
    return AsyncMapConsumer<DistdirInfo, std::pair<nlohmann::json, bool>>(
        distdir_to_git, jobs);
}
