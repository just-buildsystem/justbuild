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

#include "src/buildtool/file_system/symlinks_map/resolve_symlinks_map.hpp"

#include "fmt/core.h"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/storage/config.hpp"

namespace {

void ResolveKnownEntry(GitObjectToResolve const& obj,
                       GitRepo::TreeEntryInfo const& entry_info,
                       GitCASPtr const& just_git_cas,
                       ResolveSymlinksMap::SetterPtr const& setter,
                       ResolveSymlinksMap::LoggerPtr const& logger,
                       ResolveSymlinksMap::SubCallerPtr const& subcaller) {
    // differentiated treatment based on object type
    if (IsFileObject(entry_info.type)) {
        // files are already resolved, so return the hash directly
        (*setter)(ResolvedGitObject{.id = entry_info.id,
                                    .type = entry_info.type,
                                    .path = obj.rel_path});
    }
    else if (IsTreeObject(entry_info.type)) {
        // for tree types we resolve by rebuilding the tree from the
        // resolved children
        auto just_git_repo = GitRepo::Open(just_git_cas);
        if (not just_git_repo) {
            (*logger)("ResolveSymlinks: could not open Git cache repository!",
                      /*fatal=*/true);
            return;
        }
        auto children = just_git_repo->ReadTree(
            entry_info.id,
            [](std::vector<bazel_re::Digest> const& /*unused*/) {
                return true;
            },
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
                        if (auto target = just_git_cas->ReadObject(raw_id)) {
                            children_info.emplace_back(
                                obj.root_tree_id,
                                obj.rel_path / e.name,
                                obj.pragma_special,
                                std::make_optional(GitRepo::TreeEntryInfo{
                                    .id = ToHexString(raw_id),
                                    .type = e.type,
                                    .symlink_content = *target}));
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
                                .symlink_content = std::nullopt});
                    }
                }
            }
        }
        (*subcaller)(
            children_info,
            [children_info, parent = obj, just_git_cas, setter, logger](
                auto const& resolved_entries) {
                // create the entries map of the children
                GitRepo::tree_entries_t entries{};
                auto num = resolved_entries.size();
                entries.reserve(num);
                for (auto i = 0; i < num; ++i) {
                    auto const& p = children_info[i].rel_path;
                    entries[*FromHexString(resolved_entries[i]->id)]
                        .emplace_back(
                            p.filename().string(),  // we only need the name
                            resolved_entries[i]->type);
                }
                // create the tree inside our Git CAS, which is already
                // existing by this point. Also, this operation is
                // guarded internally, so no need for the
                // critical_git_op map
                auto just_git_repo = GitRepo::Open(just_git_cas);
                if (not just_git_repo) {
                    (*logger)(
                        "ResolveSymlinks: could not open Git cache repository!",
                        /*fatal=*/true);
                    return;
                }
                auto tree_raw_id = just_git_repo->CreateTree(entries);
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
        // if partially resolved, return non-upwards symlinks as-is
        if (obj.pragma_special == PragmaSpecial::ResolvePartially and
            PathIsNonUpwards(*entry_info.symlink_content)) {
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
                                /*known_info=*/std::nullopt)},
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
        // look up entry by its relative path
        auto just_git_cas = GitCAS::Open(StorageConfig::GitRoot());
        if (not just_git_cas) {
            (*logger)("ResolveSymlinks: could not open Git cache database!",
                      /*fatal=*/true);
            return;
        }
        auto just_git_repo = GitRepo::Open(just_git_cas);
        if (not just_git_repo) {
            (*logger)("ResolveSymlinks: could not open Git cache repository!",
                      /*fatal=*/true);
            return;
        }
        auto entry_info = key.known_info
                              ? key.known_info
                              : just_git_repo->GetObjectByPathFromTree(
                                    key.root_tree_id, key.rel_path);

        // differentiate between existing path and non-existing
        if (entry_info) {
            ResolveKnownEntry(
                key, *entry_info, just_git_cas, setter, logger, subcaller);
        }
        else {
            // non-existing paths come from symlinks, so treat accordingly
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
                                    /*known_info=*/std::nullopt)},
                [key,
                 parent_path,
                 filename = key.rel_path.filename(),
                 just_git_cas,
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
                    auto just_git_repo = GitRepo::Open(just_git_cas);
                    if (not just_git_repo) {
                        (*logger)(
                            "ResolveSymlinks: could not open Git cache "
                            "repository!",
                            /*fatal=*/true);
                        return;
                    }
                    auto entry_info = just_git_repo->GetObjectByPathFromTree(
                        resolved_parent.id, filename);
                    if (entry_info) {
                        ResolveKnownEntry(
                            GitObjectToResolve(key.root_tree_id,
                                               resolved_parent.path / filename,
                                               key.pragma_special,
                                               /*known_info=*/std::nullopt),
                            std::move(*entry_info),
                            just_git_cas,
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
