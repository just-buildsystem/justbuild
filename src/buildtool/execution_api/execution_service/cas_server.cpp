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
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/execution_service/cas_utils.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
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

static auto ChunkingAlgorithmToString(::bazel_re::ChunkingAlgorithm_Value type)
    -> std::string {
    switch (type) {
        case ::bazel_re::ChunkingAlgorithm_Value::
            ChunkingAlgorithm_Value_IDENTITY:
            return "IDENTITY";
        case ::bazel_re::ChunkingAlgorithm_Value::
            ChunkingAlgorithm_Value_RABINCDC_8KB:
            return "RABINCDC_8KB";
        case ::bazel_re::ChunkingAlgorithm_Value::
            ChunkingAlgorithm_Value_FASTCDC:
            return "FASTCDC";
        default:
            return "[Unknown Chunking Algorithm Type]";
    }
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
            if (auto err = CASUtils::EnsureTreeInvariant(
                    x.digest(), x.data(), *storage_)) {
                auto str = fmt::format("BatchUpdateBlobs: {}", *err);
                logger_.Emit(LogLevel::Error, str);
                return ::grpc::Status{grpc::StatusCode::FAILED_PRECONDITION,
                                      str};
            }
            auto const& dgst = storage_->CAS().StoreTree(x.data());
            if (!dgst) {
                auto const& str = fmt::format(
                    "BatchUpdateBlobs: could not upload tree {}", hash);
                logger_.Emit(LogLevel::Error, str);
                return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
            }
            if (auto err = CheckDigestConsistency(x.digest(), *dgst)) {
                auto str = fmt::format("BatchUpdateBlobs: {}", *err);
                logger_.Emit(LogLevel::Error, str);
                return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
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
                auto str = fmt::format("BatchUpdateBlobs: {}", *err);
                logger_.Emit(LogLevel::Error, str);
                return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
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
    ::grpc::ServerWriter<::bazel_re::GetTreeResponse>* /*writer*/)
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

    auto chunking_algorithm = request->chunking_algorithm();
    logger_.Emit(LogLevel::Debug,
                 "SplitBlob({}, {})",
                 blob_digest.hash(),
                 ChunkingAlgorithmToString(chunking_algorithm));

    // Print warning if unsupported chunking algorithm was requested.
    if (chunking_algorithm != ::bazel_re::ChunkingAlgorithm_Value::
                                  ChunkingAlgorithm_Value_IDENTITY and
        chunking_algorithm != ::bazel_re::ChunkingAlgorithm_Value::
                                  ChunkingAlgorithm_Value_FASTCDC) {
        logger_.Emit(LogLevel::Warning,
                     fmt::format("SplitBlob: unsupported chunking algorithm "
                                 "{}, will use "
                                 "default implementation {}",
                                 ChunkingAlgorithmToString(chunking_algorithm),
                                 ChunkingAlgorithmToString(
                                     ::bazel_re::ChunkingAlgorithm_Value::
                                         ChunkingAlgorithm_Value_FASTCDC)));
    }

    // Acquire garbage collection lock.
    auto lock = GarbageCollector::SharedLock();
    if (not lock) {
        auto str =
            fmt::format("SplitBlob: could not acquire garbage collection lock");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    // Split blob into chunks.
    auto split_result =
        chunking_algorithm == ::bazel_re::ChunkingAlgorithm_Value::
                                  ChunkingAlgorithm_Value_IDENTITY
            ? CASUtils::SplitBlobIdentity(blob_digest, *storage_)
            : CASUtils::SplitBlobFastCDC(blob_digest, *storage_);

    if (std::holds_alternative<grpc::Status>(split_result)) {
        auto status = std::get<grpc::Status>(split_result);
        auto str = fmt::format("SplitBlob: {}", status.error_message());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{status.error_code(), str};
    }

    auto chunk_digests = std::get<std::vector<bazel_re::Digest>>(split_result);
    logger_.Emit(LogLevel::Debug, [&blob_digest, &chunk_digests]() {
        std::stringstream ss{};
        ss << "Split blob " << blob_digest.hash() << ":"
           << blob_digest.size_bytes() << " into " << chunk_digests.size()
           << " chunks: [ ";
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

auto CASServiceImpl::SpliceBlob(::grpc::ServerContext* /*context*/,
                                const ::bazel_re::SpliceBlobRequest* request,
                                ::bazel_re::SpliceBlobResponse* response)
    -> ::grpc::Status {
    if (not request->has_blob_digest()) {
        auto str = fmt::format("SpliceBlob: no blob digest provided");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    auto const& blob_digest = request->blob_digest();
    if (not IsValidHash(blob_digest.hash())) {
        auto str = fmt::format("SpliceBlob: unsupported digest {}",
                               blob_digest.hash());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    logger_.Emit(LogLevel::Debug,
                 "SpliceBlob({}, {} chunks)",
                 blob_digest.hash(),
                 request->chunk_digests().size());

    // Acquire garbage collection lock.
    auto lock = GarbageCollector::SharedLock();
    if (not lock) {
        auto str = fmt::format(
            "SpliceBlob: could not acquire garbage collection lock");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    // Splice blob from chunks.
    auto chunk_digests = std::vector<bazel_re::Digest>{};
    std::copy(request->chunk_digests().cbegin(),
              request->chunk_digests().cend(),
              std::back_inserter(chunk_digests));
    auto splice_result =
        CASUtils::SpliceBlob(blob_digest, chunk_digests, *storage_);
    if (std::holds_alternative<grpc::Status>(splice_result)) {
        auto status = std::get<grpc::Status>(splice_result);
        auto str = fmt::format("SpliceBlob: {}", status.error_message());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{status.error_code(), str};
    }
    auto digest = std::get<bazel_re::Digest>(splice_result);
    if (auto err = CheckDigestConsistency(blob_digest, digest)) {
        auto str = fmt::format("SpliceBlob: {}", *err);
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }
    response->mutable_blob_digest()->CopyFrom(digest);
    return ::grpc::Status::OK;
}
