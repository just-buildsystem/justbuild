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
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/rpc/status.pb.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/execution_service/cas_utils.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/expected.hpp"

auto CASServiceImpl::FindMissingBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::FindMissingBlobsRequest* request,
    ::bazel_re::FindMissingBlobsResponse* response) -> ::grpc::Status {
    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto kStr =
            "FindMissingBlobs: could not acquire SharedLock";
        logger_.Emit(LogLevel::Error, "{}", kStr);
        return grpc::Status{grpc::StatusCode::INTERNAL, kStr};
    }
    for (auto const& x : request->blob_digests()) {
        auto const digest = ArtifactDigestFactory::FromBazel(
            storage_config_.hash_function.GetType(), x);

        bool is_in_cas = false;
        if (digest) {
            logger_.Emit(
                LogLevel::Trace, "FindMissingBlobs: {}", digest->hash());
            is_in_cas =
                digest->IsTree()
                    ? storage_.CAS().TreePath(*digest).has_value()
                    : storage_.CAS().BlobPath(*digest, false).has_value();
        }
        else {
            logger_.Emit(LogLevel::Error,
                         "FindMissingBlobs: unsupported digest {}",
                         x.hash());
        }

        if (not is_in_cas) {
            *response->add_missing_blob_digests() = x;
        }
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::BatchUpdateBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::BatchUpdateBlobsRequest* request,
    ::bazel_re::BatchUpdateBlobsResponse* response) -> ::grpc::Status {
    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto kStr =
            "BatchUpdateBlobs: could not acquire SharedLock";
        logger_.Emit(LogLevel::Error, "{}", kStr);
        return grpc::Status{grpc::StatusCode::INTERNAL, kStr};
    }
    auto const hash_type = storage_config_.hash_function.GetType();
    for (auto const& x : request->requests()) {
        auto const& hash = x.digest().hash();
        logger_.Emit(LogLevel::Trace, "BatchUpdateBlobs: {}", hash);
        auto const digest =
            ArtifactDigestFactory::FromBazel(hash_type, x.digest());
        if (not digest) {
            auto const str =
                fmt::format("BatchUpdateBlobs: unsupported digest {}", hash);
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
        }
        logger_.Emit(LogLevel::Trace, "BatchUpdateBlobs: {}", digest->hash());
        auto* r = response->add_responses();
        r->mutable_digest()->CopyFrom(x.digest());

        auto const status = CASUtils::AddDataToCAS(*digest, x.data(), storage_);
        if (not status.ok()) {
            auto const str =
                fmt::format("BatchUpdateBlobs: {}", status.error_message());
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{status.error_code(), str};
        }
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::BatchReadBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::BatchReadBlobsRequest* request,
    ::bazel_re::BatchReadBlobsResponse* response) -> ::grpc::Status {
    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto kStr =
            "BatchReadBlobs: Could not acquire SharedLock";
        logger_.Emit(LogLevel::Error, "{}", kStr);
        return grpc::Status{grpc::StatusCode::INTERNAL, kStr};
    }
    for (auto const& x : request->digests()) {
        auto* r = response->add_responses();
        r->mutable_digest()->CopyFrom(x);

        auto const digest = ArtifactDigestFactory::FromBazel(
            storage_config_.hash_function.GetType(), x);
        if (not digest) {
            auto const str =
                fmt::format("BatchReadBlobs: unsupported digest {}", x.hash());
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
        }
        auto const path =
            digest->IsTree()
                ? storage_.CAS().TreePath(*digest)
                : storage_.CAS().BlobPath(*digest, /*is_executable=*/false);

        if (not path) {
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
    static constexpr auto kStr = "GetTree not implemented";
    logger_.Emit(LogLevel::Error, "{}", kStr);
    return ::grpc::Status{grpc::StatusCode::UNIMPLEMENTED, kStr};
}

auto CASServiceImpl::SplitBlob(::grpc::ServerContext* /*context*/,
                               const ::bazel_re::SplitBlobRequest* request,
                               ::bazel_re::SplitBlobResponse* response)
    -> ::grpc::Status {
    if (not request->has_blob_digest()) {
        static constexpr auto kStr = "SplitBlob: no blob digest provided";
        logger_.Emit(LogLevel::Error, "{}", kStr);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, kStr};
    }

    auto const blob_digest = ArtifactDigestFactory::FromBazel(
        storage_config_.hash_function.GetType(), request->blob_digest());
    if (not blob_digest) {
        auto const str = fmt::format("SplitBlob: unsupported digest {}",
                                     request->blob_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    // Acquire garbage collection lock.
    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto kStr =
            "SplitBlob: could not acquire garbage collection lock";
        logger_.Emit(LogLevel::Error, "{}", kStr);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, kStr};
    }

    // Split blob into chunks.
    auto const split_result =
        CASUtils::SplitBlobFastCDC(*blob_digest, storage_);

    if (not split_result) {
        auto const& status = split_result.error();
        auto const str = fmt::format("SplitBlob: {}", status.error_message());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{status.error_code(), str};
    }

    auto const& chunk_digests = *split_result;
    logger_.Emit(LogLevel::Debug, [&blob_digest, &chunk_digests]() {
        std::stringstream ss{};
        ss << "Split blob " << blob_digest->hash() << ":" << blob_digest->size()
           << " into " << chunk_digests.size() << " chunks: [ ";
        for (auto const& chunk_digest : chunk_digests) {
            ss << chunk_digest.hash() << ":" << chunk_digest.size() << " ";
        }
        ss << "]";
        return ss.str();
    });

    std::transform(chunk_digests.cbegin(),
                   chunk_digests.cend(),
                   pb::back_inserter(response->mutable_chunk_digests()),
                   [](ArtifactDigest const& digest) {
                       return ArtifactDigestFactory::ToBazel(digest);
                   });
    return ::grpc::Status::OK;
}

auto CASServiceImpl::SpliceBlob(::grpc::ServerContext* /*context*/,
                                const ::bazel_re::SpliceBlobRequest* request,
                                ::bazel_re::SpliceBlobResponse* response)
    -> ::grpc::Status {
    if (not request->has_blob_digest()) {
        static constexpr auto kStr = "SpliceBlob: no blob digest provided";
        logger_.Emit(LogLevel::Error, "{}", kStr);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, kStr};
    }

    auto const hash_type = storage_config_.hash_function.GetType();
    auto const blob_digest =
        ArtifactDigestFactory::FromBazel(hash_type, request->blob_digest());
    if (not blob_digest) {
        auto const str = fmt::format("SpliceBlob: unsupported digest {}",
                                     request->blob_digest().hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    logger_.Emit(LogLevel::Debug,
                 "SpliceBlob({}, {} chunks)",
                 blob_digest->hash(),
                 request->chunk_digests().size());

    auto chunk_digests = std::vector<ArtifactDigest>{};
    chunk_digests.reserve(request->chunk_digests().size());
    for (auto const& x : request->chunk_digests()) {
        auto chunk = ArtifactDigestFactory::FromBazel(hash_type, x);
        if (not chunk) {
            auto const str =
                fmt::format("SpliceBlob: unsupported digest {}", x.hash());
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
        }
        chunk_digests.emplace_back(*std::move(chunk));
    }

    // Acquire garbage collection lock.
    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto kStr =
            "SpliceBlob: could not acquire garbage collection lock";
        logger_.Emit(LogLevel::Error, "{}", kStr);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, kStr};
    }

    // Splice blob from chunks.
    auto const splice_result =
        CASUtils::SpliceBlob(*blob_digest, chunk_digests, storage_);
    if (not splice_result) {
        auto const& status = splice_result.error();
        auto const str = fmt::format("SpliceBlob: {}", status.error_message());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{status.error_code(), str};
    }

    (*response->mutable_blob_digest()) =
        ArtifactDigestFactory::ToBazel(*splice_result);
    return ::grpc::Status::OK;
}
