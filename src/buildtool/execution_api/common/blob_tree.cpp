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

#include "src/buildtool/execution_api/common/blob_tree.hpp"

#include <utility>
#include <variant>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/hex_string.hpp"

/// NOLINTNEXTLINE(misc-no-recursion)
auto BlobTree::FromDirectoryTree(DirectoryTreePtr const& tree,
                                 std::filesystem::path const& parent) noexcept
    -> std::optional<BlobTreePtr> {
    GitRepo::tree_entries_t entries;
    std::vector<BlobTreePtr> nodes;
    try {
        entries.reserve(tree->size());
        for (auto const& [name, node] : *tree) {
            if (std::holds_alternative<DirectoryTreePtr>(node)) {
                auto const& dir = std::get<DirectoryTreePtr>(node);
                auto blob_tree = FromDirectoryTree(dir, parent / name);
                if (not blob_tree) {
                    return std::nullopt;
                }
                auto raw_id = FromHexString((*blob_tree)->Blob().digest.hash());
                if (not raw_id) {
                    return std::nullopt;
                }
                entries[std::move(*raw_id)].emplace_back(name,
                                                         ObjectType::Tree);
                // Only add tree objects to the blob tree to be uploaded.
                nodes.emplace_back((*blob_tree));
            }
            else {
                auto const& artifact = std::get<Artifact const*>(node);
                auto const& object_info = artifact->Info();
                if (not object_info) {
                    return std::nullopt;
                }
                auto raw_id = FromHexString(object_info->digest.hash());
                if (not raw_id) {
                    return std::nullopt;
                }
                entries[std::move(*raw_id)].emplace_back(name,
                                                         object_info->type);
            }
        }
        if (auto git_tree = GitRepo::CreateShallowTree(entries)) {
            ArtifactDigest digest{ToHexString(git_tree->first),
                                  git_tree->second.size(),
                                  /*is_tree=*/true};
            return std::make_shared<BlobTree>(ArtifactBlob{std::move(digest),
                                                           git_tree->second,
                                                           /*is_exec=*/false},
                                              nodes);
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}
