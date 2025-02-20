// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BYTESTREAM_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BYTESTREAM_CLIENT_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>  // std::move

#include <grpcpp/grpcpp.h>

#include "google/bytestream/bytestream.grpc.pb.h"
#include "google/bytestream/bytestream.pb.h"
#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/execution_api/common/artifact_blob.hpp"
#include "src/buildtool/execution_api/common/bytestream_utils.hpp"
#include "src/buildtool/execution_api/common/ids.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/incremental_reader.hpp"

/// Implements client side for google.bytestream.ByteStream service.
class ByteStreamClient {
  public:
    class IncrementalReader {
        friend class ByteStreamClient;

      public:
        /// \brief Read next chunk of data.
        /// \returns empty string if stream finished and std::nullopt on error.
        [[nodiscard]] auto Next() -> std::optional<std::string> {
            google::bytestream::ReadResponse response{};
            if (reader_->Read(&response)) {
                return std::move(*response.mutable_data());
            }

            if (not finished_) {
                auto status = reader_->Finish();
                if (not status.ok()) {
                    logger_->Emit(LogLevel::Debug,
                                  "{}: {}",
                                  static_cast<int>(status.error_code()),
                                  status.error_message());
                    return std::nullopt;
                }
                finished_ = true;
            }
            return std::string{};
        }

      private:
        Logger const* logger_;
        grpc::ClientContext ctx_;
        std::unique_ptr<grpc::ClientReader<google::bytestream::ReadResponse>>
            reader_;
        bool finished_{false};

        IncrementalReader(
            gsl::not_null<google::bytestream::ByteStream::Stub*> const& stub,
            std::string const& instance_name,
            ArtifactDigest const& digest,
            Logger const* logger)
            : logger_{logger} {
            google::bytestream::ReadRequest request{};
            request.set_resource_name(
                ByteStreamUtils::ReadRequest::ToString(instance_name, digest));
            reader_ = stub->Read(&ctx_, request);
        }
    };

    explicit ByteStreamClient(std::string const& server,
                              Port port,
                              gsl::not_null<Auth const*> const& auth) noexcept {
        stub_ = google::bytestream::ByteStream::NewStub(
            CreateChannelWithCredentials(server, port, auth));
    }

    [[nodiscard]] auto IncrementalRead(
        std::string const& instance_name,
        ArtifactDigest const& digest) const noexcept -> IncrementalReader {
        return IncrementalReader{stub_.get(), instance_name, digest, &logger_};
    }

    [[nodiscard]] auto Read(std::string const& instance_name,
                            ArtifactDigest const& digest) const noexcept
        -> std::optional<std::string> {
        auto reader = IncrementalRead(instance_name, digest);
        std::string output{};
        auto data = reader.Next();
        while (data and not data->empty()) {
            output.append(data->begin(), data->end());
            data = reader.Next();
        }
        if (not data) {
            return std::nullopt;
        }
        return output;
    }

    [[nodiscard]] auto Write(std::string const& instance_name,
                             ArtifactBlob const& blob) const noexcept -> bool {
        thread_local static std::string uuid{};
        if (uuid.empty()) {
            auto id = CreateProcessUniqueId();
            if (not id) {
                logger_.Emit(LogLevel::Debug,
                             "Failed creating process unique id.");
                return false;
            }
            uuid = CreateUUIDVersion4(*id);
        }

        try {
            grpc::ClientContext ctx;
            google::bytestream::WriteResponse response{};
            auto writer = stub_->Write(&ctx, &response);

            auto const resource_name = ByteStreamUtils::WriteRequest::ToString(
                instance_name, uuid, blob.digest);

            google::bytestream::WriteRequest request{};
            request.set_resource_name(resource_name);
            request.mutable_data()->reserve(ByteStreamUtils::kChunkSize);

            auto const to_read = ::IncrementalReader::FromMemory(
                ByteStreamUtils::kChunkSize, &*blob.data);
            if (not to_read.has_value()) {
                logger_.Emit(
                    LogLevel::Error,
                    "ByteStreamClient: Failed to create a reader for {}:\n{}",
                    request.resource_name(),
                    to_read.error());
                return false;
            }

            std::size_t pos = 0;
            for (auto it = to_read->begin(); it != to_read->end();) {
                auto const chunk = *it;
                if (not chunk.has_value()) {
                    logger_.Emit(
                        LogLevel::Error,
                        "ByteStreamClient: Failed to read data for {}:\n{}",
                        request.resource_name(),
                        chunk.error());
                    return false;
                }
                *request.mutable_data() = *chunk;

                request.set_write_offset(static_cast<int>(pos));
                request.set_finish_write(pos + chunk->size() >=
                                         blob.data->size());
                if (writer->Write(request)) {
                    pos += chunk->size();
                    ++it;
                }
                else {
                    // According to the docs, quote:
                    // If there is an error or the connection is broken during
                    // the `Write()`, the client should check the status of the
                    // `Write()` by calling `QueryWriteStatus()` and continue
                    // writing from the returned `committed_size`.
                    auto const committed_size =
                        QueryWriteStatus(request.resource_name());
                    if (committed_size <= 0) {
                        logger_.Emit(
                            LogLevel::Warning,
                            "broken stream for upload to resource name {}",
                            request.resource_name());
                        return false;
                    }
                    pos = gsl::narrow<std::size_t>(committed_size);
                    it = to_read->make_iterator(pos);
                }
            }
            if (not writer->WritesDone()) {
                logger_.Emit(LogLevel::Warning,
                             "broken stream for upload to resource name {}",
                             request.resource_name());
                return false;
            }

            auto status = writer->Finish();
            if (not status.ok()) {
                LogStatus(&logger_, LogLevel::Debug, status);
                return false;
            }
            if (gsl::narrow<std::size_t>(response.committed_size()) !=
                blob.data->size()) {
                logger_.Emit(
                    LogLevel::Warning,
                    "Commited size {} is different from the original one {}.",
                    response.committed_size(),
                    blob.data->size());
                return false;
            }
            return true;
        } catch (...) {
            logger_.Emit(LogLevel::Warning, "Caught exception in Write");
            return false;
        }
    }

  private:
    std::unique_ptr<google::bytestream::ByteStream::Stub> stub_;
    Logger logger_{"ByteStreamClient"};

    [[nodiscard]] auto QueryWriteStatus(
        std::string const& resource_name) const noexcept -> std::int64_t {
        grpc::ClientContext ctx;
        google::bytestream::QueryWriteStatusRequest request{};
        request.set_resource_name(resource_name);
        google::bytestream::QueryWriteStatusResponse response{};
        stub_->QueryWriteStatus(&ctx, request, &response);
        return response.committed_size();
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BYTESTREAM_CLIENT_HPP
