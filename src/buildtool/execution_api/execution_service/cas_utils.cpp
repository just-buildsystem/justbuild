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

#include "fmt/core.h"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

static auto ToGrpc(LargeObjectError&& error) noexcept -> grpc::Status {
    switch (error.Code()) {
        case LargeObjectErrorCode::Internal:
            return grpc::Status{grpc::StatusCode::INTERNAL,
                                std::move(error).Message()};
        case LargeObjectErrorCode::FileNotFound:
            return grpc::Status{grpc::StatusCode::NOT_FOUND,
                                std::move(error).Message()};
        case LargeObjectErrorCode::InvalidResult:
        case LargeObjectErrorCode::InvalidTree:
            return grpc::Status{grpc::StatusCode::FAILED_PRECONDITION,
                                std::move(error).Message()};
    }
    return grpc::Status{grpc::StatusCode::INTERNAL, "an unknown error"};
}

auto CASUtils::EnsureTreeInvariant(bazel_re::Digest const& digest,
                                   std::string const& tree_data,
                                   Storage const& storage) noexcept
    -> std::optional<std::string> {
    auto error = storage.CAS().CheckTreeInvariant(digest, tree_data);
    if (error) {
        return std::move(*error).Message();
    }
    return std::nullopt;
}

auto CASUtils::SplitBlobIdentity(bazel_re::Digest const& blob_digest,
                                 Storage const& storage) noexcept
    -> expected<std::vector<bazel_re::Digest>, grpc::Status> {

    // Check blob existence.
    auto path =
        NativeSupport::IsTree(blob_digest.hash())
            ? storage.CAS().TreePath(static_cast<ArtifactDigest>(blob_digest))
            : storage.CAS().BlobPath(static_cast<ArtifactDigest>(blob_digest),
                                     false);
    if (not path) {
        return unexpected{
            grpc::Status{grpc::StatusCode::NOT_FOUND,
                         fmt::format("blob not found {}", blob_digest.hash())}};
    }

    // The split protocol states that each chunk that is returned by the
    // operation is stored in (file) CAS. This means for the native mode, if we
    // return the identity of a tree, we need to put the tree data in file CAS
    // and return the resulting digest.
    auto chunk_digests = std::vector<bazel_re::Digest>{};
    if (NativeSupport::IsTree(blob_digest.hash())) {
        auto tree_data = FileSystemManager::ReadFile(*path);
        if (not tree_data) {
            return unexpected{grpc::Status{
                grpc::StatusCode::INTERNAL,
                fmt::format("could read tree data {}", blob_digest.hash())}};
        }
        auto digest = storage.CAS().StoreBlob(*tree_data, false);
        if (not digest) {
            return unexpected{
                grpc::Status{grpc::StatusCode::INTERNAL,
                             fmt::format("could not store tree as blob {}",
                                         blob_digest.hash())}};
        }
        chunk_digests.emplace_back(*digest);
        return chunk_digests;
    }
    chunk_digests.emplace_back(blob_digest);
    return chunk_digests;
}

auto CASUtils::SplitBlobFastCDC(bazel_re::Digest const& blob_digest,
                                Storage const& storage) noexcept
    -> expected<std::vector<bazel_re::Digest>, grpc::Status> {
    // Split blob into chunks:
    auto split = NativeSupport::IsTree(blob_digest.hash())
                     ? storage.CAS().SplitTree(blob_digest)
                     : storage.CAS().SplitBlob(blob_digest);

    // Process result:
    if (split) {
        return *std::move(split);
    }
    // Process errors
    return unexpected{ToGrpc(std::move(split).error())};
}

auto CASUtils::SpliceBlob(bazel_re::Digest const& blob_digest,
                          std::vector<bazel_re::Digest> const& chunk_digests,
                          Storage const& storage) noexcept
    -> expected<bazel_re::Digest, grpc::Status> {
    // Splice blob from chunks:
    auto splice =
        NativeSupport::IsTree(blob_digest.hash())
            ? storage.CAS().SpliceTree(blob_digest, chunk_digests)
            : storage.CAS().SpliceBlob(blob_digest, chunk_digests, false);

    // Process result:
    if (splice) {
        return *std::move(splice);
    }
    return unexpected{ToGrpc(std::move(splice).error())};
}
