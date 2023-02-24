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

#ifndef BYTESTREAM_SERVER_HPP
#define BYTESTREAM_SERVER_HPP

#include "google/bytestream/bytestream.grpc.pb.h"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/storage.hpp"

class BytestreamServiceImpl : public ::google::bytestream::ByteStream::Service {
  public:
    // `Read()` is used to retrieve the contents of a resource as a sequence
    // of bytes. The bytes are returned in a sequence of responses, and the
    // responses are delivered as the results of a server-side streaming RPC.
    auto Read(::grpc::ServerContext* context,
              const ::google::bytestream::ReadRequest* request,
              ::grpc::ServerWriter< ::google::bytestream::ReadResponse>* writer)
        -> ::grpc::Status override;
    // `Write()` is used to send the contents of a resource as a sequence of
    // bytes. The bytes are sent in a sequence of request protos of a
    // client-side streaming RPC.
    //
    // A `Write()` action is resumable. If there is an error or the connection
    // is broken during the `Write()`, the client should check the status of the
    // `Write()` by calling `QueryWriteStatus()` and continue writing from the
    // returned `committed_size`. This may be less than the amount of data the
    // client previously sent.
    //
    // Calling `Write()` on a resource name that was previously written and
    // finalized could cause an error, depending on whether the underlying
    // service allows over-writing of previously written resources.
    //
    // When the client closes the request channel, the service will respond with
    // a `WriteResponse`. The service will not view the resource as `complete`
    // until the client has sent a `WriteRequest` with `finish_write` set to
    // `true`. Sending any requests on a stream after sending a request with
    // `finish_write` set to `true` will cause an error. The client **should**
    // check the `WriteResponse` it receives to determine how much data the
    // service was able to commit and whether the service views the resource as
    // `complete` or not.
    auto Write(
        ::grpc::ServerContext* context,
        ::grpc::ServerReader< ::google::bytestream::WriteRequest>* reader,
        ::google::bytestream::WriteResponse* response)
        -> ::grpc::Status override;
    // `QueryWriteStatus()` is used to find the `committed_size` for a resource
    // that is being written, which can then be used as the `write_offset` for
    // the next `Write()` call.
    //
    // If the resource does not exist (i.e., the resource has been deleted, or
    // the first `Write()` has not yet reached the service), this method returns
    // the error `NOT_FOUND`.
    //
    // The client **may** call `QueryWriteStatus()` at any time to determine how
    // much data has been processed for this resource. This is useful if the
    // client is buffering data and needs to know which data can be safely
    // evicted. For any sequence of `QueryWriteStatus()` calls for a given
    // resource name, the sequence of returned `committed_size` values will be
    // non-decreasing.
    auto QueryWriteStatus(
        ::grpc::ServerContext* context,
        const ::google::bytestream::QueryWriteStatusRequest* request,
        ::google::bytestream::QueryWriteStatusResponse* response)
        -> ::grpc::Status override;

  private:
    gsl::not_null<Storage const*> storage_ = &Storage::Instance();
    Logger logger_{"execution-service:bytestream"};
};

#endif
