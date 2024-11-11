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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>  // std::move

#include <grpcpp/grpcpp.h>

#include "google/bytestream/bytestream.grpc.pb.h"
#include "google/bytestream/bytestream.pb.h"
#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/execution_api/common/bytestream_utils.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

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
            ByteStreamUtils::ReadRequest&& read_request,
            Logger const* logger)
            : logger_{logger} {
            google::bytestream::ReadRequest request{};
            request.set_resource_name(std::move(read_request).ToString());
            reader_ = stub->Read(&ctx_, request);
        }
    };

    explicit ByteStreamClient(std::string const& server,
                              Port port,
                              gsl::not_null<Auth const*> const& auth) noexcept {
        stub_ = google::bytestream::ByteStream::NewStub(
            CreateChannelWithCredentials(server, port, auth));
    }

    [[nodiscard]] auto IncrementalRead(ByteStreamUtils::ReadRequest&& request)
        const noexcept -> IncrementalReader {
        return IncrementalReader{stub_.get(), std::move(request), &logger_};
    }

    [[nodiscard]] auto Read(ByteStreamUtils::ReadRequest&& request)
        const noexcept -> std::optional<std::string> {
        auto reader = IncrementalRead(std::move(request));
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

    [[nodiscard]] auto Write(ByteStreamUtils::WriteRequest&& write_request,
                             std::string const& data) const noexcept -> bool {
        try {
            grpc::ClientContext ctx;
            google::bytestream::WriteResponse response{};
            auto writer = stub_->Write(&ctx, &response);

            google::bytestream::WriteRequest request{};
            request.set_resource_name(std::move(write_request).ToString());
            request.mutable_data()->resize(ByteStreamUtils::kChunkSize, '\0');

            std::size_t pos = 0;
            do {  // NOLINT(cppcoreguidelines-avoid-do-while)
                auto const size =
                    std::min(data.size() - pos, ByteStreamUtils::kChunkSize);
                request.mutable_data()->resize(size);
                data.copy(request.mutable_data()->data(), size, pos);
                request.set_write_offset(static_cast<int>(pos));
                request.set_finish_write(pos + size >= data.size());
                if (not writer->Write(request)) {
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
                }
                else {
                    pos += ByteStreamUtils::kChunkSize;
                }
            } while (pos < data.size());
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
                data.size()) {
                logger_.Emit(
                    LogLevel::Warning,
                    "Commited size {} is different from the original one {}.",
                    response.committed_size(),
                    data.size());
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
