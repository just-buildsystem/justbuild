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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_TREE_READER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_TREE_READER_HPP

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/common/tree_reader_utils.hpp"
#include "src/buildtool/file_system/object_type.hpp"

struct ReadTreeResult final {
    std::vector<std::filesystem::path> paths;
    std::vector<Artifact::ObjectInfo> infos;
};

template <typename TImpl>
class TreeReader final {
  public:
    template <typename... Args>
    explicit TreeReader(Args&&... args) noexcept
        : impl_(std::forward<Args>(args)...) {}

    /// \brief Reads the flat content of a tree and returns object infos of all
    /// its direct entries (trees and blobs).
    /// \param tree_digest          Digest of the tree.
    /// \param parent               Local parent path.
    /// \returns A struct containing filesystem paths and object infos.
    [[nodiscard]] auto ReadDirectTreeEntries(
        ArtifactDigest const& digest,
        std::filesystem::path const& parent) const noexcept
        -> std::optional<ReadTreeResult> {
        ReadTreeResult result;

        TreeReaderUtils::InfoStoreFunc store_info =
            [&result, &parent](std::filesystem::path const& path,
                               Artifact::ObjectInfo&& info) {
                result.paths.emplace_back(parent / path);
                result.infos.emplace_back(std::move(info));
                return true;
            };

        if (not impl_.IsNativeProtocol()) {
            auto tree = impl_.ReadDirectory(digest);
            if (tree and
                not TreeReaderUtils::ReadObjectInfos(*tree, store_info)) {
                return std::nullopt;
            }
        }
        else {
            auto tree = impl_.ReadGitTree(digest);
            if (tree and
                not TreeReaderUtils::ReadObjectInfos(*tree, store_info)) {
                return std::nullopt;
            }
        }
        return result;
    }

    /// \brief Traverses a tree recursively and retrieves object infos of all
    /// found blobs (leafs). Tree objects are by default not added to the result
    /// list, but converted to a path name.
    /// \param tree_digest          Digest of the tree.
    /// \param parent               Local parent path.
    /// \param include_trees        Include leaf tree objects (empty trees).
    /// \returns A struct containing filesystem paths and object infos.
    [[nodiscard]] auto RecursivelyReadTreeLeafs(
        ArtifactDigest const& digest,
        std::filesystem::path const& parent,
        bool include_trees = false) const noexcept
        -> std::optional<ReadTreeResult> {
        ReadTreeResult result;

        auto store = [&result](std::filesystem::path const& path,
                               Artifact::ObjectInfo const& info) {
            result.paths.emplace_back(path);
            result.infos.emplace_back(info);
            return true;
        };

        try {
            if (ReadObjectInfosRecursively(
                    store, parent, digest, include_trees)) {
                return result;
            }
            return std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

  private:
    TImpl impl_;

    [[nodiscard]] static auto IsDirectoryEmpty(
        bazel_re::Directory const& dir) noexcept -> bool {
        return dir.files().empty() and dir.directories().empty() and
               dir.symlinks().empty();
    }

    [[nodiscard]] auto ReadObjectInfosRecursively(
        TreeReaderUtils::InfoStoreFunc const& store,
        std::filesystem::path const& parent,
        ArtifactDigest const& digest,
        bool const include_trees) const -> bool {
        TreeReaderUtils::InfoStoreFunc internal_store =
            [this, &store, &parent, include_trees](
                std::filesystem::path const& path,
                Artifact::ObjectInfo&& info) -> bool {
            return IsTreeObject(info.type)
                       ? ReadObjectInfosRecursively(
                             store, parent / path, info.digest, include_trees)
                       : store(parent / path, std::move(info));
        };

        if (not impl_.IsNativeProtocol()) {
            if (auto tree = impl_.ReadDirectory(digest)) {
                if (include_trees and IsDirectoryEmpty(*tree)) {
                    if (not store(parent, {digest, ObjectType::Tree})) {
                        return false;
                    }
                }
                return TreeReaderUtils::ReadObjectInfos(*tree, internal_store);
            }
        }
        else {
            if (auto tree = impl_.ReadGitTree(digest)) {
                if (include_trees and tree->empty()) {
                    if (not store(parent, {digest, ObjectType::Tree})) {
                        return false;
                    }
                }
                return TreeReaderUtils::ReadObjectInfos(*tree, internal_store);
            }
        }
        return false;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_TREE_READER_HPP
