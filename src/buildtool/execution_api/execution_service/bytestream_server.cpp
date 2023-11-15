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

#include "bytestream_server.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "fmt/core.h"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/common/bytestream_common.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "src/utils/cpp/verify_hash.hpp"

namespace {
auto ParseResourceName(std::string const& x) -> std::optional<std::string> {
    // resource name is like this
    // remote-execution/uploads/c4f03510-7d56-4490-8934-01bce1b1288e/blobs/62183d7a696acf7e69e218efc82c93135f8c85f895/4424712
    if (auto end = x.rfind('/'); end != std::string::npos) {
        if (auto start = x.rfind('/', end - 1); start != std::string::npos) {
            return x.substr(start + 1, end - start - 1);
        }
    }
    return std::nullopt;
}
}  // namespace

auto BytestreamServiceImpl::Read(
    ::grpc::ServerContext* /*context*/,
    const ::google::bytestream::ReadRequest* request,
    ::grpc::ServerWriter<::google::bytestream::ReadResponse>* writer)
    -> ::grpc::Status {
    logger_.Emit(LogLevel::Trace, "Read {}", request->resource_name());
    // resource_name is of type
    // remote-execution/blobs/62f408d64bca5de775c4b1dbc3288fc03afd6b19eb/0
    std::istringstream iss{request->resource_name()};
    auto hash = ParseResourceName(request->resource_name());
    if (!hash) {
        auto str = fmt::format("could not parse {}", request->resource_name());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    if (auto error_msg = IsAHash(*hash); error_msg) {
        logger_.Emit(LogLevel::Debug, *error_msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *error_msg};
    }

    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    std::optional<std::filesystem::path> path{};

    if (NativeSupport::IsTree(*hash)) {
        ArtifactDigest dgst{NativeSupport::Unprefix(*hash), 0, true};
        path = storage_->CAS().TreePath(static_cast<bazel_re::Digest>(dgst));
    }
    else {
        ArtifactDigest dgst{NativeSupport::Unprefix(*hash), 0, false};
        path = storage_->CAS().BlobPath(static_cast<bazel_re::Digest>(dgst),
                                        false);
    }
    if (!path) {
        auto str = fmt::format("could not find {}", *hash);
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{::grpc::StatusCode::NOT_FOUND, str};
    }
    std::ifstream blob{*path};

    ::google::bytestream::ReadResponse response;
    std::string buffer(kChunkSize, '\0');
    bool done = false;
    blob.seekg(request->read_offset());
    while (!done) {
        blob.read(buffer.data(), kChunkSize);
        if (blob.eof()) {
            // do not send random bytes
            buffer.resize(static_cast<std::size_t>(blob.gcount()));
            done = true;
        }
        *(response.mutable_data()) = buffer;
        writer->Write(response);
    }
    return ::grpc::Status::OK;
}

auto BytestreamServiceImpl::Write(
    ::grpc::ServerContext* /*context*/,
    ::grpc::ServerReader<::google::bytestream::WriteRequest>* reader,
    ::google::bytestream::WriteResponse* response) -> ::grpc::Status {
    ::google::bytestream::WriteRequest request;
    reader->Read(&request);
    logger_.Emit(LogLevel::Debug, "write {}", request.resource_name());
    auto hash = ParseResourceName(request.resource_name());
    if (!hash) {
        auto str = fmt::format("could not parse {}", request.resource_name());
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, str};
    }
    if (auto error_msg = IsAHash(*hash); error_msg) {
        logger_.Emit(LogLevel::Debug, *error_msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *error_msg};
    }
    logger_.Emit(LogLevel::Trace,
                 "Write: {}, offset {}, finish write {}",
                 *hash,
                 request.write_offset(),
                 request.finish_write());
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto str = fmt::format("Could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    auto tmp_dir = TmpDir::Create(StorageConfig::GenerationCacheRoot(0) /
                                  "execution-service");
    if (!tmp_dir) {
        return ::grpc::Status{::grpc::StatusCode::INTERNAL,
                              "could not create TmpDir"};
    }
    auto tmp = tmp_dir->GetPath() / *hash;
    {
        std::ofstream of{tmp, std::ios::binary};
        of.write(request.data().data(),
                 static_cast<std::streamsize>(request.data().size()));
    }
    while (!request.finish_write() && reader->Read(&request)) {
        std::ofstream of{tmp, std::ios::binary | std::ios::app};
        of.write(request.data().data(),
                 static_cast<std::streamsize>(request.data().size()));
    }
    if (NativeSupport::IsTree(*hash)) {
        if (not storage_->CAS().StoreTree</*kOwner=*/true>(tmp)) {
            auto str = fmt::format("could not store tree {}", *hash);
            logger_.Emit(LogLevel::Error, str);
            return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, str};
        }
    }
    else {
        if (not storage_->CAS().StoreBlob</*kOwner=*/true>(
                tmp, /*is_executable=*/false)) {
            auto str = fmt::format("could not store blob {}", *hash);
            logger_.Emit(LogLevel::Error, str);
            return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, str};
        }
    }
    response->set_committed_size(
        static_cast<google::protobuf::int64>(std::filesystem::file_size(tmp)));
    return ::grpc::Status::OK;
}

auto BytestreamServiceImpl::QueryWriteStatus(
    ::grpc::ServerContext* /*context*/,
    const ::google::bytestream::QueryWriteStatusRequest* /*request*/,
    ::google::bytestream::QueryWriteStatusResponse* /*response*/)
    -> ::grpc::Status {
    auto const* str = "QueryWriteStatus not implemented";
    logger_.Emit(LogLevel::Error, str);
    return ::grpc::Status{grpc::StatusCode::UNIMPLEMENTED, str};
}
