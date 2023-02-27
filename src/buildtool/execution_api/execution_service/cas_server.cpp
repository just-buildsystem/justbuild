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

#include "fmt/format.h"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/local/garbage_collector.hpp"

static constexpr std::size_t kJustHashLength = 42;
static constexpr std::size_t kSHA256Length = 64;

static auto IsValidHash(std::string const& x) -> bool {
    auto const& length = x.size();
    return (Compatibility::IsCompatible() and length == kSHA256Length) or
           length == kJustHashLength;
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
            if (!storage_.TreePath(x)) {
                auto* d = response->add_missing_blob_digests();
                d->CopyFrom(x);
            }
        }
        else if (!storage_.BlobPath(x, false)) {
            auto* d = response->add_missing_blob_digests();
            d->CopyFrom(x);
        }
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::CheckDigestConsistency(
    std::string const& ref,
    std::string const& computed) const noexcept -> std::optional<std::string> {
    if (ref != computed) {
        auto const& str = fmt::format(
            "Blob {} is corrupted: digest and data do not correspond", ref);
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
            auto const& dgst = storage_.StoreTree(x.data());
            if (!dgst) {
                auto const& str = fmt::format(
                    "BatchUpdateBlobs: could not upload tree {}", hash);
                logger_.Emit(LogLevel::Error, str);
                return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
            }
            if (auto err = CheckDigestConsistency(hash, dgst->hash())) {
                return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, *err};
            }
        }
        else {
            auto const& dgst = storage_.StoreBlob(x.data(), false);
            if (!dgst) {
                auto const& str = fmt::format(
                    "BatchUpdateBlobs: could not upload blob {}", hash);
                logger_.Emit(LogLevel::Error, str);
                return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
            }
            if (auto err = CheckDigestConsistency(hash, dgst->hash())) {
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
            path = storage_.TreePath(digest);
        }
        else {
            path = storage_.BlobPath(digest, false);
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
