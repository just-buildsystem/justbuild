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

#include <cstddef>
#include <fstream>
#include <sstream>
#include <utility>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/common/bytestream_common.hpp"
#include "src/buildtool/execution_api/execution_service/cas_utils.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

namespace {
auto ParseResourceName(std::string const& x) noexcept
    -> std::optional<bazel_re::Digest> {
    // resource name is like this
    // remote-execution/uploads/c4f03510-7d56-4490-8934-01bce1b1288e/blobs/62183d7a696acf7e69e218efc82c93135f8c85f895/4424712
    auto const size_delim = x.rfind('/');
    if (size_delim == std::string::npos) {
        return std::nullopt;
    }

    auto const hash_delim = x.rfind('/', size_delim - 1);
    if (hash_delim == std::string::npos) {
        return std::nullopt;
    }

    try {
        bazel_re::Digest digest{};

        digest.set_size_bytes(std::stoll(x.substr(size_delim + 1)));
        digest.set_hash(x.substr(hash_delim + 1, size_delim - hash_delim - 1));
        return digest;
    } catch (...) {
        return std::nullopt;
    }
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
    auto const digest = ParseResourceName(request->resource_name());
    if (not digest) {
        auto const str =
            fmt::format("could not parse {}", request->resource_name());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    auto const read_digest = ArtifactDigestFactory::FromBazel(
        storage_config_.hash_function.GetType(), *digest);
    if (not read_digest) {
        logger_.Emit(LogLevel::Debug, "{}", read_digest.error());
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT,
                              read_digest.error()};
    }

    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto str = "Could not acquire SharedLock";
        logger_.Emit(LogLevel::Error, "{}", str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    auto const path =
        read_digest->IsTree()
            ? storage_.CAS().TreePath(*read_digest)
            : storage_.CAS().BlobPath(*read_digest, /*is_executable=*/false);

    if (not path) {
        auto const str = fmt::format("could not find {}", read_digest->hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{::grpc::StatusCode::NOT_FOUND, str};
    }

    std::ifstream stream{*path, std::ios::binary};
    stream.seekg(request->read_offset(), std::ios::beg);

    ::google::bytestream::ReadResponse response;
    std::string& buffer = *response.mutable_data();
    buffer.resize(kChunkSize);

    while (not stream.eof()) {
        stream.read(buffer.data(), kChunkSize);
        if (stream.bad()) {
            auto const str =
                fmt::format("Failed to read data for {}", read_digest->hash());
            logger_.Emit(LogLevel::Error, str);
            return grpc::Status{grpc::StatusCode::INTERNAL, str};
        }

        if (stream.eof()) {
            // do not send random bytes
            buffer.resize(static_cast<std::size_t>(stream.gcount()));
        }
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
    auto const digest = ParseResourceName(request.resource_name());
    if (not digest) {
        auto const str =
            fmt::format("could not parse {}", request.resource_name());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    auto const write_digest = ArtifactDigestFactory::FromBazel(
        storage_config_.hash_function.GetType(), *digest);
    if (not write_digest) {
        logger_.Emit(LogLevel::Debug, "{}", write_digest.error());
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT,
                              write_digest.error()};
    }
    logger_.Emit(LogLevel::Trace,
                 "Write: {}, offset {}, finish write {}",
                 write_digest->hash(),
                 request.write_offset(),
                 request.finish_write());
    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto str = "Could not acquire SharedLock";
        logger_.Emit(LogLevel::Error, "{}", str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    auto const tmp_dir = storage_config_.CreateTypedTmpDir("execution-service");
    if (not tmp_dir) {
        return ::grpc::Status{::grpc::StatusCode::INTERNAL,
                              "could not create TmpDir"};
    }

    auto tmp = tmp_dir->GetPath() / write_digest->hash();
    {
        std::ofstream stream{tmp, std::ios::binary};
        do {
            if (not stream.good()) {
                auto const str = fmt::format("Failed to write data for {}",
                                             write_digest->hash());
                logger_.Emit(LogLevel::Error, "{}", str);
                return ::grpc::Status{::grpc::StatusCode::INTERNAL, str};
            }
            stream.write(request.data().data(),
                         static_cast<std::streamsize>(request.data().size()));
        } while (not request.finish_write() and reader->Read(&request));
    }

    // Before storing a tree, we have to verify that its parts are present
    if (write_digest->IsTree()) {
        // ... unfortunately, this requires us to read the whole tree object
        // into memory
        auto const content = FileSystemManager::ReadFile(tmp);
        if (not content) {
            auto const msg =
                fmt::format("Failed to read temporary file {} for {}",
                            tmp.string(),
                            write_digest->hash());
            logger_.Emit(LogLevel::Error, "{}", msg);
            return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
        }

        if (auto err = CASUtils::EnsureTreeInvariant(
                *write_digest, *content, storage_)) {
            auto const str = fmt::format("Write: {}", *std::move(err));
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::FAILED_PRECONDITION, str};
        }
    }

    // Store blob and verify hash
    static constexpr bool kOwner = true;
    auto const stored =
        write_digest->IsTree()
            ? storage_.CAS().StoreTree<kOwner>(tmp)
            : storage_.CAS().StoreBlob<kOwner>(tmp, /*is_executable=*/false);
    if (not stored) {
        // This is a serious problem: we have a sequence of bytes, but cannot
        // write them to CAS.
        auto const str =
            fmt::format("Failed to store object {}", write_digest->hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, str};
    }

    if (*stored != *write_digest) {
        // User error: did not get a file with the announced hash
        auto const str =
            fmt::format("In upload for {} received object with hash {}",
                        write_digest->hash(),
                        stored->hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, str};
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
    static constexpr auto str = "QueryWriteStatus not implemented";
    logger_.Emit(LogLevel::Error, "{}", str);
    return ::grpc::Status{grpc::StatusCode::UNIMPLEMENTED, str};
}
