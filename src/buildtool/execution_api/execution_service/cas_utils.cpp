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

#include "src/buildtool/execution_api/execution_service/cas_utils.hpp"

#include <fstream>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/execution_service/file_chunker.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/fs_utils.hpp"
#include "src/utils/cpp/hex_string.hpp"

auto CASUtils::EnsureTreeInvariant(std::string const& data,
                                   std::string const& hash,
                                   Storage const& storage) noexcept
    -> std::optional<std::string> {
    auto entries = GitRepo::ReadTreeData(
        data,
        NativeSupport::Unprefix(hash),
        [](auto const& /*unused*/) { return true; },
        /*is_hex_id=*/true);
    if (not entries) {
        return fmt::format("could not read tree data {}", hash);
    }
    for (auto const& entry : *entries) {
        for (auto const& item : entry.second) {
            auto digest = static_cast<bazel_re::Digest>(
                ArtifactDigest{ToHexString(entry.first),
                               /*size is unknown*/ 0,
                               IsTreeObject(item.type)});
            if (not(IsTreeObject(item.type)
                        ? storage.CAS().TreePath(digest)
                        : storage.CAS().BlobPath(digest, false))) {
                return fmt::format(
                    "tree invariant violated {}: missing element {}",
                    hash,
                    digest.hash());
            }
            // The GitRepo::tree_entries_t data structure maps the object id to
            // a list of entries of that object in possibly multiple trees. It
            // is sufficient to check the existence of only one of these entries
            // to be sure that the object is in CAS since they all have the same
            // content.
            break;
        }
    }
    return std::nullopt;
}

auto CASUtils::SplitBlobIdentity(bazel_re::Digest const& blob_digest,
                                 Storage const& storage) noexcept
    -> std::variant<std::vector<bazel_re::Digest>, grpc::Status> {

    // Check blob existence.
    auto path = NativeSupport::IsTree(blob_digest.hash())
                    ? storage.CAS().TreePath(blob_digest)
                    : storage.CAS().BlobPath(blob_digest, false);
    if (not path) {
        return grpc::Status{
            grpc::StatusCode::NOT_FOUND,
            fmt::format("blob not found {}", blob_digest.hash())};
    }

    // The split protocol states that each chunk that is returned by the
    // operation is stored in (file) CAS. This means for the native mode, if we
    // return the identity of a tree, we need to put the tree data in file CAS
    // and return the resulting digest.
    auto chunk_digests = std::vector<bazel_re::Digest>{};
    if (NativeSupport::IsTree(blob_digest.hash())) {
        auto tree_data = FileSystemManager::ReadFile(*path);
        if (not tree_data) {
            return grpc::Status{
                grpc::StatusCode::INTERNAL,
                fmt::format("could read tree data {}", blob_digest.hash())};
        }
        auto digest = storage.CAS().StoreBlob(*tree_data, false);
        if (not digest) {
            return grpc::Status{grpc::StatusCode::INTERNAL,
                                fmt::format("could not store tree as blob {}",
                                            blob_digest.hash())};
        }
        chunk_digests.emplace_back(*digest);
        return chunk_digests;
    }
    chunk_digests.emplace_back(blob_digest);
    return chunk_digests;
}

auto CASUtils::SplitBlobFastCDC(bazel_re::Digest const& blob_digest,
                                Storage const& storage) noexcept
    -> std::variant<std::vector<bazel_re::Digest>, grpc::Status> {

    // Check blob existence.
    auto path = NativeSupport::IsTree(blob_digest.hash())
                    ? storage.CAS().TreePath(blob_digest)
                    : storage.CAS().BlobPath(blob_digest, false);
    if (not path) {
        return grpc::Status{
            grpc::StatusCode::NOT_FOUND,
            fmt::format("blob not found {}", blob_digest.hash())};
    }

    // Split blob into chunks, store each chunk in CAS, and collect chunk
    // digests.
    auto chunker = FileChunker{*path};
    if (not chunker.IsOpen()) {
        return grpc::Status{
            grpc::StatusCode::INTERNAL,
            fmt::format("could not open blob {}", blob_digest.hash())};
    }
    auto chunk_digests = std::vector<bazel_re::Digest>{};
    while (auto chunk = chunker.NextChunk()) {
        auto chunk_digest = storage.CAS().StoreBlob(*chunk, false);
        if (not chunk_digest) {
            return grpc::Status{grpc::StatusCode::INTERNAL,
                                fmt::format("could not store chunk of blob {}",
                                            blob_digest.hash())};
        }
        chunk_digests.emplace_back(*chunk_digest);
    }
    if (not chunker.Finished()) {
        return grpc::Status{
            grpc::StatusCode::INTERNAL,
            fmt::format("could not split blob {}", blob_digest.hash())};
    }

    return chunk_digests;
}

auto CASUtils::SpliceBlob(bazel_re::Digest const& blob_digest,
                          std::vector<bazel_re::Digest> const& chunk_digests,
                          Storage const& storage) noexcept
    -> std::variant<bazel_re::Digest, grpc::Status> {

    // Assemble blob from chunks.
    auto tmp_dir = StorageUtils::CreateTypedTmpDir("splice");
    auto tmp_file = tmp_dir->GetPath() / "blob";
    {
        std::ofstream tmp(tmp_file, std::ios::binary);
        for (auto const& chunk_digest : chunk_digests) {
            // Check chunk existence (only check file CAS).
            auto path = storage.CAS().BlobPath(chunk_digest, false);
            if (not path) {
                return grpc::Status{
                    grpc::StatusCode::NOT_FOUND,
                    fmt::format("chunk not found {}", chunk_digest.hash())};
            }
            // Load chunk data.
            auto chunk_data = FileSystemManager::ReadFile(*path);
            if (not chunk_data) {
                return grpc::Status{grpc::StatusCode::INTERNAL,
                                    fmt::format("could read chunk data {}",
                                                chunk_digest.hash())};
            }
            tmp << *chunk_data;
        }
    }

    // Store resulting blob in according CAS.
    auto const& hash = blob_digest.hash();
    if (NativeSupport::IsTree(hash)) {
        auto const& digest =
            storage.CAS().StoreTree</* kOwner= */ true>(tmp_file);
        if (not digest) {
            return grpc::Status{grpc::StatusCode::INTERNAL,
                                fmt::format("could not store tree {}", hash)};
        }
        return *digest;
    }

    auto const& digest =
        storage.CAS().StoreBlob</* kOwner= */ true>(tmp_file, false);
    if (not digest) {
        return grpc::Status{grpc::StatusCode::INTERNAL,
                            fmt::format("could not store blob {}", hash)};
    }
    return *digest;
}
