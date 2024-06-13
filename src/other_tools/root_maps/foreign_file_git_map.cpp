// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/other_tools/root_maps/foreign_file_git_map.hpp"

#include "fmt/core.h"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/root_maps/root_utils.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {

void WithRootImportedToGit(ForeignFileInfo const& key,
                           std::pair<std::string, GitCASPtr> const& result,
                           ForeignFileGitMap::SetterPtr const& setter,
                           ForeignFileGitMap::LoggerPtr const& logger) {
    if (not result.second) {
        (*logger)("Importing to git failed", /*fatal=*/true);
        return;
    }
    auto tree_id_file = StorageUtils::GetForeignFileTreeIDFile(
        key.archive.content, key.name, key.executable);
    auto cache_written =
        StorageUtils::WriteTreeIDFile(tree_id_file, result.first);
    if (not cache_written) {
        (*logger)(
            fmt::format("Failed to write cache file {}", tree_id_file.string()),
            /*fatal=*/false);
    }
    (*setter)(
        std::pair(nlohmann::json::array({FileRoot::kGitTreeMarker,
                                         result.first,
                                         StorageConfig::GitRoot().string()}),
                  /*is_cache_hit=*/false));
}

void WithFetchedFile(ForeignFileInfo const& key,
                     gsl::not_null<ImportToGitMap*> const& import_to_git_map,
                     gsl::not_null<TaskSystem*> const& ts,
                     ForeignFileGitMap::SetterPtr const& setter,
                     ForeignFileGitMap::LoggerPtr const& logger) {
    auto tmp_dir = StorageConfig::CreateTypedTmpDir("foreign-file");
    auto const& cas = Storage::Instance().CAS();
    auto digest = ArtifactDigest(key.archive.content, 0, key.executable);
    auto content_cas_path = cas.BlobPath(digest, key.executable);
    if (not content_cas_path) {
        (*logger)(
            fmt::format("Failed to locally find {} after fetching for repo {}",
                        key.archive.content,
                        nlohmann::json(key.archive.origin).dump()),
            true);
        return;
    }
    auto did_create_hardlink = FileSystemManager::CreateFileHardlink(
        *content_cas_path, tmp_dir->GetPath() / key.name, LogLevel::Warning);
    if (not did_create_hardlink) {
        (*logger)(fmt::format(
                      "Failed to hard link {} as {} in temporary directory {}",
                      content_cas_path->string(),
                      nlohmann::json(key.name).dump(),
                      tmp_dir->GetPath().string()),
                  true);
        return;
    }
    CommitInfo c_info{
        tmp_dir->GetPath(),
        fmt::format("foreign file at {}", nlohmann::json(key.name).dump()),
        key.archive.content};
    import_to_git_map->ConsumeAfterKeysReady(
        ts,
        {std::move(c_info)},
        [tmp_dir,  // keep tmp_dir alive
         key,
         setter,
         logger](auto const& values) {
            WithRootImportedToGit(key, *values[0], setter, logger);
        },
        [logger, target_path = tmp_dir->GetPath()](auto const& msg,
                                                   bool fatal) {
            (*logger)(fmt::format("While importing target {} to Git:\n{}",
                                  target_path.string(),
                                  msg),
                      fatal);
        });
}

void UseCacheHit(const std::string& tree_id,
                 ForeignFileGitMap::SetterPtr const& setter) {
    // We keep the invariant, that, whenever a cache entry is written,
    // the root is in our git root; in particular, the latter is present,
    // initialized, etc; so we can directly write the result.
    (*setter)(
        std::pair(nlohmann::json::array({FileRoot::kGitTreeMarker,
                                         tree_id,
                                         StorageConfig::GitRoot().string()}),
                  /*is_cache_hit=*/true));
}

void HandleAbsentForeignFile(ForeignFileInfo const& key,
                             std::optional<ServeApi> const& serve,
                             ForeignFileGitMap::SetterPtr const& setter,
                             ForeignFileGitMap::LoggerPtr const& logger) {
    // Compute tree in memory
    GitRepo::tree_entries_t entries{};
    auto raw_id = FromHexString(key.archive.content);
    if (not raw_id) {
        (*logger)(fmt::format("Failure converting {} to raw id.",
                              key.archive.content),
                  true);
        return;
    }
    entries[*raw_id].emplace_back(
        key.name, key.executable ? ObjectType::Executable : ObjectType::File);
    auto tree = GitRepo::CreateShallowTree(entries);
    if (not tree) {
        (*logger)(fmt::format("Failure to construct in-memory tree with entry "
                              "{} at place {}",
                              key.archive.content,
                              nlohmann::json(key.name).dump()),
                  true);
        return;
    }
    auto tree_id = ToHexString(tree->first);
    if (serve) {
        auto has_tree = CheckServeHasAbsentRoot(*serve, tree_id, logger);
        if (not has_tree) {
            return;
        }
        if (*has_tree) {
            (*setter)(std::pair(
                nlohmann::json::array({FileRoot::kGitTreeMarker, tree_id}),
                /*is_cache_hit=*/false));
            return;
        }
        auto serve_result = serve->RetrieveTreeFromForeignFile(
            key.archive.content, key.name, key.executable);
        if (std::holds_alternative<std::string>(serve_result)) {
            // if serve has set up the tree, it must match what we
            // expect
            auto const& served_tree_id = std::get<std::string>(serve_result);
            if (tree_id != served_tree_id) {
                (*logger)(fmt::format("Mismatch in served root tree "
                                      "id: expected {}, but got {}",
                                      tree_id,
                                      served_tree_id),
                          /*fatal=*/true);
                return;
            }
            // set workspace root as absent
            (*setter)(std::pair(
                nlohmann::json::array({FileRoot::kGitTreeMarker, tree_id}),
                /*is_cache_hit=*/false));
            return;
        }
        auto const& is_fatal = std::get<bool>(serve_result);
        if (is_fatal) {
            (*logger)(fmt::format("Serve endpoint failed to set up root "
                                  "from known foreign-file content {}",
                                  key.archive.content),
                      /*fatal=*/true);
            return;
        }
        (*logger)(
            fmt::format("Failed to set up root via serve, continuing anyway"),
            /*fatal=*/false);
    }
    else {
        (*logger)(fmt::format("Workspace root {} marked absent but no "
                              "serve endpoint provided.",
                              tree_id),
                  /*fatal=*/false);
    }
    (*setter)(
        std::pair(nlohmann::json::array({FileRoot::kGitTreeMarker, tree_id}),
                  false /*no cache hit*/));
}
}  // namespace

[[nodiscard]] auto CreateForeignFileGitMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    gsl::not_null<ImportToGitMap*> const& import_to_git_map,
    std::optional<ServeApi> const& serve,
    bool fetch_absent,
    std::size_t jobs) -> ForeignFileGitMap {
    auto setup_foreign_file =
        [content_cas_map, import_to_git_map, fetch_absent, &serve](
            auto ts,
            auto setter,
            auto logger,
            auto /* unused */,
            auto const& key) {
            if (key.absent and not fetch_absent) {
                HandleAbsentForeignFile(key, serve, setter, logger);
                return;
            }
            auto tree_id_file = StorageUtils::GetForeignFileTreeIDFile(
                key.archive.content, key.name, key.executable);
            if (FileSystemManager::Exists(tree_id_file)) {
                auto tree_id = FileSystemManager::ReadFile(tree_id_file);
                if (not tree_id) {
                    (*logger)(fmt::format("Failed to read tree id from file {}",
                                          tree_id_file.string()),
                              /*fatal=*/true);
                    return;
                }
                UseCacheHit(*tree_id, setter);
                return;
            }
            content_cas_map->ConsumeAfterKeysReady(
                ts,
                {key.archive},
                [key, import_to_git_map, setter, logger, ts](
                    [[maybe_unused]] auto const& values) {
                    WithFetchedFile(key, import_to_git_map, ts, setter, logger);
                },
                [logger, content = key.archive.content](auto const& msg,
                                                        bool fatal) {
                    (*logger)(fmt::format("While ensuring content {} is in "
                                          "CAS:\n{}",
                                          content,
                                          msg),
                              fatal);
                });
        };
    return AsyncMapConsumer<ForeignFileInfo, std::pair<nlohmann::json, bool>>(
        setup_foreign_file, jobs);
}
