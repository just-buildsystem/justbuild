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

#include "src/buildtool/execution_api/execution_service/cas_server.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/execution_service/file_chunker.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/verify_hash.hpp"

static constexpr std::size_t kGitSHA1Length = 42;
static constexpr std::size_t kSHA256Length = 64;

static auto IsValidHash(std::string const& x) -> bool {
    auto error_msg = IsAHash(x);
    auto const& length = x.size();
    return !error_msg and
           ((Compatibility::IsCompatible() and length == kSHA256Length) or
            length == kGitSHA1Length);
}

auto CASServiceImpl::FindMissingBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::FindMissingBlobsRequest* request,
    ::bazel_re::FindMissingBlobsResponse* response) -> ::grpc::Status {
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str =
            fmt::format("FindMissingBlobs: could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    for (auto const& x : request->blob_digests()) {
        auto const& hash = x.hash();

        if (!IsValidHash(hash)) {
            logger_.Emit(LogLevel::Error,
                         "FindMissingBlobs: unsupported digest {}",
                         hash);
            auto* d = response->add_missing_blob_digests();
            d->CopyFrom(x);
            continue;
        }
        logger_.Emit(LogLevel::Trace, "FindMissingBlobs: {}", hash);
        if (NativeSupport::IsTree(hash)) {
            if (!storage_->CAS().TreePath(x)) {
                auto* d = response->add_missing_blob_digests();
                d->CopyFrom(x);
            }
        }
        else if (!storage_->CAS().BlobPath(x, false)) {
            auto* d = response->add_missing_blob_digests();
            d->CopyFrom(x);
        }
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::CheckDigestConsistency(bazel_re::Digest const& ref,
                                            bazel_re::Digest const& computed)
    const noexcept -> std::optional<std::string> {
    if (ref.hash() != computed.hash() or
        ((Compatibility::IsCompatible() or ref.size_bytes() > 0LL) and
         ref.size_bytes() != computed.size_bytes())) {
        auto const& str = fmt::format(
            "Blob {} is corrupted: provided digest {}:{} and digest computed "
            "from data {}:{} do not correspond.",
            ref.hash(),
            ref.hash(),
            ref.size_bytes(),
            computed.size_bytes(),
            computed.size_bytes());
        logger_.Emit(LogLevel::Error, str);
        return str;
    }
    return std::nullopt;
}

auto CASServiceImpl::EnsureTreeInvariant(std::string const& data,
                                         std::string const& hash) const noexcept
    -> std::optional<std::string> {
    auto entries = GitRepo::ReadTreeData(
        data,
        NativeSupport::Unprefix(hash),
        [](auto const& /*unused*/) { return true; },
        /*is_hex_id=*/true);
    if (not entries) {
        auto str = fmt::format("Could not read tree data {}", hash);
        logger_.Emit(LogLevel::Error, str);
        return str;
    }
    for (auto const& entry : *entries) {
        for (auto const& item : entry.second) {
            auto digest = static_cast<bazel_re::Digest>(
                ArtifactDigest{ToHexString(entry.first),
                               /*size is unknown*/ 0,
                               IsTreeObject(item.type)});
            if (not(IsTreeObject(item.type)
                        ? storage_->CAS().TreePath(digest)
                        : storage_->CAS().BlobPath(digest, false))) {
                auto str = fmt::format(
                    "Tree invariant violated {}: missing element {}",
                    hash,
                    digest.hash());
                logger_.Emit(LogLevel::Error, str);
                return str;
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

auto CASServiceImpl::BatchUpdateBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::BatchUpdateBlobsRequest* request,
    ::bazel_re::BatchUpdateBlobsResponse* response) -> ::grpc::Status {
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str =
            fmt::format("BatchUpdateBlobs: could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    for (auto const& x : request->requests()) {
        auto const& hash = x.digest().hash();
        logger_.Emit(LogLevel::Trace, "BatchUpdateBlobs: {}", hash);
        if (!IsValidHash(hash)) {
            auto const& str =
                fmt::format("BatchUpdateBlobs: unsupported digest {}", hash);
            logger_.Emit(LogLevel::Error, str);
            return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
        }
        logger_.Emit(LogLevel::Trace, "BatchUpdateBlobs: {}", hash);
        auto* r = response->add_responses();
        r->mutable_digest()->CopyFrom(x.digest());
        if (NativeSupport::IsTree(hash)) {
            // In native mode: for trees, check whether the tree invariant holds
            // before storing the actual tree object.
            if (auto err = EnsureTreeInvariant(x.data(), hash)) {
                return ::grpc::Status{grpc::StatusCode::FAILED_PRECONDITION,
                                      *err};
            }
            auto const& dgst = storage_->CAS().StoreTree(x.data());
            if (!dgst) {
                auto const& str = fmt::format(
                    "BatchUpdateBlobs: could not upload tree {}", hash);
                logger_.Emit(LogLevel::Error, str);
                return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
            }
            if (auto err = CheckDigestConsistency(x.digest(), *dgst)) {
                return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, *err};
            }
        }
        else {
            auto const& dgst = storage_->CAS().StoreBlob(x.data(), false);
            if (!dgst) {
                auto const& str = fmt::format(
                    "BatchUpdateBlobs: could not upload blob {}", hash);
                logger_.Emit(LogLevel::Error, str);
                return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
            }
            if (auto err = CheckDigestConsistency(x.digest(), *dgst)) {
                return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, *err};
            }
        }
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::BatchReadBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::BatchReadBlobsRequest* request,
    ::bazel_re::BatchReadBlobsResponse* response) -> ::grpc::Status {
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("BatchReadBlobs: Could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    for (auto const& digest : request->digests()) {
        auto* r = response->add_responses();
        r->mutable_digest()->CopyFrom(digest);
        std::optional<std::filesystem::path> path;
        if (NativeSupport::IsTree(digest.hash())) {
            path = storage_->CAS().TreePath(digest);
        }
        else {
            path = storage_->CAS().BlobPath(digest, false);
        }
        if (!path) {
            google::rpc::Status status;
            status.set_code(grpc::StatusCode::NOT_FOUND);
            r->mutable_status()->CopyFrom(status);

            continue;
        }
        std::ifstream cert{*path};
        std::string tmp((std::istreambuf_iterator<char>(cert)),
                        std::istreambuf_iterator<char>());
        *(r->mutable_data()) = std::move(tmp);

        r->mutable_status()->CopyFrom(google::rpc::Status{});
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::GetTree(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::GetTreeRequest* /*request*/,
    ::grpc::ServerWriter< ::bazel_re::GetTreeResponse>* /*writer*/)
    -> ::grpc::Status {
    auto const* str = "GetTree not implemented";
    logger_.Emit(LogLevel::Error, str);
    return ::grpc::Status{grpc::StatusCode::UNIMPLEMENTED, str};
}

auto CASServiceImpl::SplitBlob(::grpc::ServerContext* /*context*/,
                               const ::bazel_re::SplitBlobRequest* request,
                               ::bazel_re::SplitBlobResponse* response)
    -> ::grpc::Status {
    if (not request->has_blob_digest()) {
        auto str = fmt::format("SplitBlob: no blob digest provided");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    auto const& blob_digest = request->blob_digest();
    if (not IsValidHash(blob_digest.hash())) {
        auto str =
            fmt::format("SplitBlob: unsupported digest {}", blob_digest.hash());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    // Acquire garbage collection lock.
    auto lock = GarbageCollector::SharedLock();
    if (not lock) {
        auto str =
            fmt::format("SplitBlob: could not acquire garbage collection lock");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    logger_.Emit(LogLevel::Info, "SplitBlob({})", blob_digest.hash());

    // Check blob existence.
    auto path = std::optional<std::filesystem::path>{};
    if (NativeSupport::IsTree(blob_digest.hash())) {
        path = storage_->CAS().TreePath(blob_digest);
    }
    else {
        path = storage_->CAS().BlobPath(blob_digest, true);
        if (not path) {
            path = storage_->CAS().BlobPath(blob_digest, false);
        }
    }
    if (not path) {
        auto str =
            fmt::format("SplitBlob: blob not found {}", blob_digest.hash());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::NOT_FOUND, str};
    }

    // Split blob into chunks, store each chunk in CAS, and collect chunk
    // digests.
    auto chunker = FileChunker{*path};
    if (not chunker.IsOpen()) {
        auto str = fmt::format("SplitBlob: could not open blob for reading");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    auto chunk_digests = std::vector<bazel_re::Digest>{};
    while (auto chunk = chunker.NextChunk()) {
        auto chunk_digest = storage_->CAS().StoreBlob(*chunk, false);
        if (not chunk_digest) {
            auto str =
                fmt::format("SplitBlob: could not store chunk of blob {}",
                            blob_digest.hash());
            logger_.Emit(LogLevel::Error, str);
            return ::grpc::Status{grpc::StatusCode::RESOURCE_EXHAUSTED, str};
        }
        chunk_digests.emplace_back(*chunk_digest);
    }
    if (not chunker.Finished()) {
        auto str =
            fmt::format("SplitBlob: could split blob {}", blob_digest.hash());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    logger_.Emit(LogLevel::Debug, [&blob_digest, &chunk_digests]() {
        std::stringstream ss{};
        ss << "Split blob " << blob_digest.hash() << ":"
           << blob_digest.size_bytes() << " into [ ";
        for (auto const& chunk_digest : chunk_digests) {
            ss << chunk_digest.hash() << ":" << chunk_digest.size_bytes()
               << " ";
        }
        ss << "]";
        return ss.str();
    });
    std::copy(chunk_digests.cbegin(),
              chunk_digests.cend(),
              pb::back_inserter(response->mutable_chunk_digests()));
    return ::grpc::Status::OK;
}
