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

#include "src/buildtool/file_system/symlinks/resolve_symlinks_map.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {

/// \brief Ensures that a given blob is in the target repo.
/// On errors, calls logger with fatal and returns false.
[[nodiscard]] auto EnsureBlobExists(GitObjectToResolve const& obj,
                                    GitRepo::TreeEntryInfo const& entry_info,
                                    ResolveSymlinksMap::LoggerPtr const& logger)
    -> bool {
    ExpectsAudit(IsBlobObject(entry_info.type));
    // check if entry is in target repo
    auto target_git_repo = GitRepo::Open(obj.target_cas);
    if (not target_git_repo) {
        (*logger)("ResolveSymlinks: could not open target Git repository!",
                  /*fatal=*/true);
        return false;
    }
    auto wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
        [logger, id = entry_info.id](auto const& msg, bool fatal) {
            (*logger)(fmt::format("ResolveSymlinks: while checking blob {} "
                                  "exists in target Git repository:\n{}",
                                  id,
                                  msg),
                      fatal);
        });
    auto has_blob =
        target_git_repo->CheckBlobExists(entry_info.id, wrapped_logger);
    if (not has_blob) {
        return false;
    }
    if (not *has_blob) {
        // copy blob from source repo to target repo, if source is not target
        if (obj.source_cas.get() == obj.target_cas.get()) {
            (*logger)(
                fmt::format("ResolveSymlinks: unexpectedly missing blob {} in "
                            "both source and target Git repositories",
                            entry_info.id),
                /*fatal=*/true);
            return false;
        }
        auto source_git_repo = GitRepo::Open(obj.source_cas);
        if (not source_git_repo) {
            (*logger)("ResolveSymlinks: could not open source Git repository",
                      /*fatal=*/true);
            return false;
        }
        wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
            [logger, id = entry_info.id](auto const& msg, bool fatal) {
                (*logger)(fmt::format("ResolveSymlinks: while checking blob {} "
                                      "exists in source Git repository:\n{}",
                                      id,
                                      msg),
                          fatal);
            });
        auto res = source_git_repo->TryReadBlob(entry_info.id, wrapped_logger);
        if (not res.first) {
            return false;  // fatal failure
        }
        if (not res.second.has_value()) {
            (*logger)(fmt::format("ResolveSymlinks: unexpectedly missing "
                                  "blob {} in source Git repository",
                                  entry_info.id),
                      /*fatal=*/true);
            return false;
        }
        // write blob in target repository
        wrapped_logger = std::make_shared<GitRepo::anon_logger_t>(
            [logger, id = entry_info.id](auto const& msg, bool fatal) {
                (*logger)(fmt::format("ResolveSymlinks: while writing blob "
                                      "{} into Git cache:\n{}",
                                      id,
                                      msg),
                          fatal);
            });
        if (not target_git_repo->WriteBlob(res.second.value(),
                                           wrapped_logger)) {
            return false;
        }
    }
    return true;  // success!
}

/// \brief Method to handle entries by their known type.
/// Guarantees to either call logger with fatal or call setter on returning.
void ResolveKnownEntry(GitObjectToResolve const& obj,
                       GitRepo::TreeEntryInfo const& entry_info,
                       ResolveSymlinksMap::SetterPtr const& setter,
                       ResolveSymlinksMap::LoggerPtr const& logger,
                       ResolveSymlinksMap::SubCallerPtr const& subcaller) {
    // differentiated treatment based on object type
    if (IsFileObject(entry_info.type)) {
        // ensure target repository has the entry
        if (not EnsureBlobExists(obj, entry_info, logger)) {
            return;
        }
        // files are already resolved, so return the hash directly
        (*setter)(ResolvedGitObject{.id = entry_info.id,
                                    .type = entry_info.type,
                                    .path = obj.rel_path});
    }
    else if (IsTreeObject(entry_info.type)) {
        // for tree types we resolve by rebuilding the tree from the
        // resolved children
        auto source_git_repo = GitRepo::Open(obj.source_cas);
        if (not source_git_repo) {
            (*logger)("ResolveSymlinks: could not open source Git repository!",
                      /*fatal=*/true);
            return;
        }
        auto children = source_git_repo->ReadTree(
            entry_info.id,
            [](auto const& /*unused*/) { return true; },
            /*is_hex_id=*/true);
        if (not children) {
            (*logger)(fmt::format("ResolveSymlinks: failed to read entries of "
                                  "subtree {} in root tree {}",
                                  entry_info.id,
                                  obj.root_tree_id),
                      /*fatal=*/true);
            return;
        }
        // resolve children
        std::vector<GitObjectToResolve> children_info{};
        children_info.reserve(children->size());
        for (auto const& [raw_id, ev] : *children) {
            for (auto const& e : ev) {
                // must enforce ignore special at the tree level!
                if (IsNonSpecialObject(e.type) or
                    obj.pragma_special != PragmaSpecial::Ignore) {
                    // children info is known, so pass this forward
                    if (IsSymlinkObject(e.type)) {
                        if (auto target = obj.source_cas->ReadObject(raw_id)) {
                            children_info.emplace_back(
                                obj.root_tree_id,
                                obj.rel_path / e.name,
                                obj.pragma_special,
                                std::make_optional(GitRepo::TreeEntryInfo{
                                    .id = ToHexString(raw_id),
                                    .type = e.type,
                                    .symlink_content = std::move(target)}),
                                obj.source_cas,
                                obj.target_cas);
                        }
                        else {
                            (*logger)(
                                fmt::format("ResolveSymlinks: could not read "
                                            "symlink {} in root tree {}",
                                            (obj.rel_path / e.name).string(),
                                            obj.root_tree_id),
                                /*fatal=*/true);
                            return;
                        }
                    }
                    else {
                        children_info.emplace_back(
                            obj.root_tree_id,
                            obj.rel_path / e.name,
                            obj.pragma_special,
                            GitRepo::TreeEntryInfo{
                                .id = ToHexString(raw_id),
                                .type = e.type,
                                .symlink_content = std::nullopt},
                            obj.source_cas,
                            obj.target_cas);
                    }
                }
            }
        }
        (*subcaller)(
            children_info,
            [children_info, parent = obj, setter, logger](
                auto const& resolved_entries) {
                // create the entries map of the children
                GitRepo::tree_entries_t entries{};
                auto num = resolved_entries.size();
                entries.reserve(num);
                for (std::size_t i = 0; i < num; ++i) {
                    auto const& p = children_info[i].rel_path;
                    entries[*FromHexString(resolved_entries[i]->id)]
                        .emplace_back(
                            p.filename().string(),  // we only need the name
                            resolved_entries[i]->type);
                }
                // create the tree inside target repo, which should already be
                // existing. This operation is guarded internally, so no need
                // for extra locking
                auto target_git_repo = GitRepo::Open(parent.target_cas);
                if (not target_git_repo) {
                    (*logger)(
                        "ResolveSymlinks: could not open target Git "
                        "repository!",
                        /*fatal=*/true);
                    return;
                }
                auto tree_raw_id = target_git_repo->CreateTree(entries);
                if (not tree_raw_id) {
                    (*logger)(fmt::format("ResolveSymlinks: failed to create "
                                          "resolved tree {} in root tree {}",
                                          parent.rel_path.string(),
                                          parent.root_tree_id),
                              /*fatal=*/true);
                    return;
                }
                // set the resolved tree hash
                (*setter)(ResolvedGitObject{.id = ToHexString(*tree_raw_id),
                                            .type = ObjectType::Tree,
                                            .path = parent.rel_path});
            },
            logger);
    }
    else {
        // sanity check: cannot resolve a symlink called with ignore
        // special, as that can only be handled by the parent tree
        if (obj.pragma_special == PragmaSpecial::Ignore) {
            (*logger)(fmt::format("ResolveSymlinks: asked to ignore symlink {} "
                                  "in root tree {}",
                                  obj.rel_path.string(),
                                  obj.root_tree_id),
                      /*fatal=*/true);
            return;
        }
        // target should have already been read
        if (not entry_info.symlink_content) {
            (*logger)(fmt::format("ResolveSymlinks: missing target of symlink "
                                  "{} in root tree {}",
                                  obj.rel_path.string(),
                                  obj.root_tree_id),
                      /*fatal=*/true);
            return;
        }
        // check if link target (unresolved) is confined to the tree
        if (not PathIsConfined(*entry_info.symlink_content, obj.rel_path)) {
            (*logger)(fmt::format("ResolveSymlinks: symlink {} is not confined "
                                  "to tree {}",
                                  obj.rel_path.string(),
                                  obj.root_tree_id),
                      /*fatal=*/true);
            return;
        }
        // if resolving partially, return a non-upwards symlink as-is
        if (obj.pragma_special == PragmaSpecial::ResolvePartially and
            PathIsNonUpwards(*entry_info.symlink_content)) {
            // ensure target repository has the entry
            if (not EnsureBlobExists(obj, entry_info, logger)) {
                return;
            }
            // return as symlink object
            (*setter)(ResolvedGitObject{.id = entry_info.id,
                                        .type = ObjectType::Symlink,
                                        .path = obj.rel_path});
            return;
        }
        // resolve the target
        auto n_target = ToNormalPath(obj.rel_path.parent_path() /
                                     *entry_info.symlink_content);
        (*subcaller)(
            {GitObjectToResolve(obj.root_tree_id,
                                n_target,
                                obj.pragma_special,
                                /*known_info=*/std::nullopt,
                                obj.source_cas,
                                obj.target_cas)},
            [setter](auto const& values) {
                (*setter)(ResolvedGitObject{*values[0]});
            },
            logger);
    }
}

}  // namespace

auto CreateResolveSymlinksMap() -> ResolveSymlinksMap {
    auto resolve_symlinks = [](auto /*unused*/,
                               auto setter,
                               auto logger,
                               auto subcaller,
                               auto const& key) {
        auto entry_info = key.known_info;
        if (not entry_info) {
            // look up entry by its relative path inside root tree if not known
            auto source_git_repo = GitRepo::Open(key.source_cas);
            if (not source_git_repo) {
                (*logger)(
                    "ResolveSymlinks: could not open source Git repository!",
                    /*fatal=*/true);
                return;
            }
            entry_info = source_git_repo->GetObjectByPathFromTree(
                key.root_tree_id, key.rel_path);
        }

        // differentiate between existing path and non-existing
        if (entry_info) {
            ResolveKnownEntry(key, *entry_info, setter, logger, subcaller);
        }
        else {
            // non-existing paths come from symlinks, so treat accordingly;
            // sanity check: pragma ignore special should not be set if here
            if (key.pragma_special == PragmaSpecial::Ignore) {
                (*logger)(
                    fmt::format("ResolveSymlinks: asked to ignore indirect "
                                "symlink path {} in root tree {}",
                                key.rel_path.string(),
                                key.root_tree_id),
                    /*fatal=*/true);
                return;
            }
            auto parent_path = key.rel_path.parent_path();
            if (parent_path == key.rel_path) {
                (*logger)(fmt::format("ResolveSymlinks: found unresolved path "
                                      "{} in root tree {}",
                                      key.rel_path.string(),
                                      key.root_tree_id),
                          /*fatal=*/true);
                return;
            }
            // resolve parent
            (*subcaller)(
                {GitObjectToResolve(key.root_tree_id,
                                    parent_path,
                                    key.pragma_special,
                                    /*known_info=*/std::nullopt,
                                    key.source_cas,
                                    key.target_cas)},
                [key,
                 parent_path,
                 filename = key.rel_path.filename(),
                 setter,
                 logger,
                 subcaller](auto const& values) {
                    auto resolved_parent = *values[0];
                    // parent must be a tree
                    if (not IsTreeObject(resolved_parent.type)) {
                        (*logger)(
                            fmt::format("ResolveSymlinks: path {} in root tree "
                                        "{} failed to resolve to a tree",
                                        parent_path.string(),
                                        key.root_tree_id),
                            /*fatal=*/true);
                        return;
                    }
                    // check if filename exists in resolved parent tree
                    auto target_git_repo = GitRepo::Open(key.target_cas);
                    if (not target_git_repo) {
                        (*logger)(
                            "ResolveSymlinks: could not open Git cache "
                            "repository!",
                            /*fatal=*/true);
                        return;
                    }
                    auto entry_info = target_git_repo->GetObjectByPathFromTree(
                        resolved_parent.id, filename);
                    if (entry_info) {
                        ResolveKnownEntry(
                            GitObjectToResolve(key.root_tree_id,
                                               resolved_parent.path / filename,
                                               key.pragma_special,
                                               /*known_info=*/std::nullopt,
                                               key.source_cas,
                                               key.target_cas),
                            std::move(*entry_info),
                            setter,
                            logger,
                            subcaller);
                    }
                    else {
                        // report unresolvable
                        (*logger)(
                            fmt::format(
                                "ResolveSymlinks: reached unresolvable "
                                "path {} in root tree {}",
                                (resolved_parent.path / filename).string(),
                                key.root_tree_id),
                            /*fatal=*/true);
                    }
                },
                logger);
        }
    };
    return AsyncMapConsumer<GitObjectToResolve, ResolvedGitObject>(
        resolve_symlinks);
}
