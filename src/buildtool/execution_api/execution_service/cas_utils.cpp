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

#include <optional>
#include <utility>

#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/large_object_cas.hpp"

namespace {
[[nodiscard]] auto ToGrpc(LargeObjectError&& error) noexcept -> grpc::Status {
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

class CASContentValidator final {
  public:
    explicit CASContentValidator(gsl::not_null<Storage const*> const& storage,
                                 bool is_owner = true) noexcept;

    template <typename TData>
    [[nodiscard]] auto Add(ArtifactDigest const& digest,
                           TData const& data) const noexcept -> grpc::Status {
        if (digest.IsTree()) {
            // For trees, check whether the tree invariant holds before storing
            // the actual tree object.
            if (auto err = storage_.CAS().CheckTreeInvariant(digest, data)) {
                return ToGrpc(std::move(*err));
            }
        }

        auto const cas_digest =
            digest.IsTree() ? StoreTree(data) : StoreBlob(data);
        if (not cas_digest) {
            // This is a serious problem: we have a sequence of bytes, but
            // cannot write them to CAS.
            return ::grpc::Status{grpc::StatusCode::INTERNAL,
                                  fmt::format("Could not upload {} {}",
                                              digest.IsTree() ? "tree" : "blob",
                                              digest.hash())};
        }

        if (auto err = CheckDigestConsistency(digest, *cas_digest)) {
            // User error: did not get a file with the announced hash
            return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT,
                                  *std::move(err)};
        }
        return ::grpc::Status::OK;
    }

  private:
    Storage const& storage_;
    bool const is_owner_;

    template <typename TData>
    [[nodiscard]] auto StoreTree(TData const& data) const noexcept
        -> std::optional<ArtifactDigest> {
        if constexpr (std::is_same_v<TData, std::string>) {
            return storage_.CAS().StoreTree(data);
        }
        else {
            return is_owner_ ? storage_.CAS().StoreTree<true>(data)
                             : storage_.CAS().StoreTree<false>(data);
        }
    }

    template <typename TData>
    [[nodiscard]] auto StoreBlob(TData const& data) const noexcept
        -> std::optional<ArtifactDigest> {
        static constexpr bool kIsExec = false;
        if constexpr (std::is_same_v<TData, std::string>) {
            return storage_.CAS().StoreBlob(data, kIsExec);
        }
        else {
            return is_owner_ ? storage_.CAS().StoreBlob<true>(data, kIsExec)
                             : storage_.CAS().StoreBlob<false>(data, kIsExec);
        }
    }

    [[nodiscard]] auto CheckDigestConsistency(ArtifactDigest const& ref,
                                              ArtifactDigest const& computed)
        const noexcept -> std::optional<std::string>;
};
}  // namespace

auto CASUtils::AddDataToCAS(ArtifactDigest const& digest,
                            std::string const& content,
                            Storage const& storage) noexcept -> grpc::Status {
    return CASContentValidator{&storage}.Add(digest, content);
}

auto CASUtils::AddFileToCAS(ArtifactDigest const& digest,
                            std::filesystem::path const& file,
                            Storage const& storage,
                            bool is_owner) noexcept -> grpc::Status {
    return CASContentValidator{&storage, is_owner}.Add(digest, file);
}

auto CASUtils::SplitBlobIdentity(ArtifactDigest const& blob_digest,
                                 Storage const& storage) noexcept
    -> expected<std::vector<ArtifactDigest>, grpc::Status> {
    // Check blob existence.
    auto path = blob_digest.IsTree()
                    ? storage.CAS().TreePath(blob_digest)
                    : storage.CAS().BlobPath(blob_digest, false);
    if (not path) {
        return unexpected{
            grpc::Status{grpc::StatusCode::NOT_FOUND,
                         fmt::format("blob not found {}", blob_digest.hash())}};
    }

    // The split protocol states that each chunk that is returned by the
    // operation is stored in (file) CAS. This means for the native mode, if we
    // return the identity of a tree, we need to put the tree data in file CAS
    // and return the resulting digest.
    auto chunk_digests = std::vector<ArtifactDigest>{};
    if (blob_digest.IsTree()) {
        auto tree_data = FileSystemManager::ReadFile(*path);
        if (not tree_data) {
            return unexpected{
                grpc::Status{grpc::StatusCode::INTERNAL,
                             fmt::format("could not read tree data {}",
                                         blob_digest.hash())}};
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

auto CASUtils::SplitBlobFastCDC(ArtifactDigest const& blob_digest,
                                Storage const& storage) noexcept
    -> expected<std::vector<ArtifactDigest>, grpc::Status> {
    // Split blob into chunks:
    auto split = blob_digest.IsTree() ? storage.CAS().SplitTree(blob_digest)
                                      : storage.CAS().SplitBlob(blob_digest);

    if (not split) {
        return unexpected{ToGrpc(std::move(split).error())};
    }
    return *std::move(split);
}

auto CASUtils::SpliceBlob(ArtifactDigest const& blob_digest,
                          std::vector<ArtifactDigest> const& chunk_digests,
                          Storage const& storage) noexcept
    -> expected<ArtifactDigest, grpc::Status> {
    // Splice blob from chunks:
    auto splice =
        blob_digest.IsTree()
            ? storage.CAS().SpliceTree(blob_digest, chunk_digests)
            : storage.CAS().SpliceBlob(blob_digest, chunk_digests, false);

    if (not splice) {
        return unexpected{ToGrpc(std::move(splice).error())};
    }
    return *std::move(splice);
}

namespace {
CASContentValidator::CASContentValidator(
    gsl::not_null<Storage const*> const& storage,
    bool is_owner) noexcept
    : storage_{*storage}, is_owner_{is_owner} {}

auto CASContentValidator::CheckDigestConsistency(ArtifactDigest const& ref,
                                                 ArtifactDigest const& computed)
    const noexcept -> std::optional<std::string> {
    bool valid = ref == computed;
    if (valid) {
        bool const check_sizes = not ProtocolTraits::IsNative(
                                     storage_.GetHashFunction().GetType()) or
                                 ref.size() != 0;
        if (check_sizes) {
            valid = ref.size() == computed.size();
        }
    }
    if (not valid) {
        return fmt::format(
            "Expected digest {}:{} and computed digest {}:{} do not match.",
            ref.hash(),
            ref.size(),
            computed.hash(),
            computed.size());
    }
    return std::nullopt;
}
}  // namespace
