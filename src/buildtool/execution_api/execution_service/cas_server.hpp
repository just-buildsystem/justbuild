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

#ifndef CAS_SERVER_HPP
#define CAS_SERVER_HPP
#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/storage.hpp"

class CASServiceImpl final
    : public bazel_re::ContentAddressableStorage::Service {
  public:
    // Determine if blobs are present in the CAS.
    //
    // Clients can use this API before uploading blobs to determine which ones
    // are already present in the CAS and do not need to be uploaded again.
    //
    // There are no method-specific errors.
    auto FindMissingBlobs(::grpc::ServerContext* context,
                          const ::bazel_re::FindMissingBlobsRequest* request,
                          ::bazel_re::FindMissingBlobsResponse* response)
        -> ::grpc::Status override;
    // Upload many blobs at once.
    //
    // The server may enforce a limit of the combined total size of blobs
    // to be uploaded using this API. This limit may be obtained using the
    // [Capabilities][build.bazel.remote.execution.v2.Capabilities] API.
    // Requests exceeding the limit should either be split into smaller
    // chunks or uploaded using the
    // [ByteStream API][google.bytestream.ByteStream], as appropriate.
    //
    // This request is equivalent to calling a Bytestream `Write` request
    // on each individual blob, in parallel. The requests may succeed or fail
    // independently.
    //
    // Errors:
    //
    // * `INVALID_ARGUMENT`: The client attempted to upload more than the
    //   server supported limit.
    //
    // Individual requests may return the following errors, additionally:
    //
    // * `RESOURCE_EXHAUSTED`: There is insufficient disk quota to store the
    // blob.
    // * `INVALID_ARGUMENT`: The
    // [Digest][build.bazel.remote.execution.v2.Digest] does not match the
    // provided data.
    auto BatchUpdateBlobs(::grpc::ServerContext* context,
                          const ::bazel_re::BatchUpdateBlobsRequest* request,
                          ::bazel_re::BatchUpdateBlobsResponse* response)
        -> ::grpc::Status override;
    // Download many blobs at once.
    //
    // The server may enforce a limit of the combined total size of blobs
    // to be downloaded using this API. This limit may be obtained using the
    // [Capabilities][build.bazel.remote.execution.v2.Capabilities] API.
    // Requests exceeding the limit should either be split into smaller
    // chunks or downloaded using the
    // [ByteStream API][google.bytestream.ByteStream], as appropriate.
    //
    // This request is equivalent to calling a Bytestream `Read` request
    // on each individual blob, in parallel. The requests may succeed or fail
    // independently.
    //
    // Errors:
    //
    // * `INVALID_ARGUMENT`: The client attempted to read more than the
    //   server supported limit.
    //
    // Every error on individual read will be returned in the corresponding
    // digest status.
    auto BatchReadBlobs(::grpc::ServerContext* context,
                        const ::bazel_re::BatchReadBlobsRequest* request,
                        ::bazel_re::BatchReadBlobsResponse* response)
        -> ::grpc::Status override;
    // Fetch the entire directory tree rooted at a node.
    //
    // This request must be targeted at a
    // [Directory][build.bazel.remote.execution.v2.Directory] stored in the
    // [ContentAddressableStorage][build.bazel.remote.execution.v2.ContentAddressableStorage]
    // (CAS). The server will enumerate the `Directory` tree recursively and
    // return every node descended from the root.
    //
    // The GetTreeRequest.page_token parameter can be used to skip ahead in
    // the stream (e.g. when retrying a partially completed and aborted
    // request), by setting it to a value taken from
    // GetTreeResponse.next_page_token of the last successfully processed
    // GetTreeResponse).
    //
    // The exact traversal order is unspecified and, unless retrieving
    // subsequent pages from an earlier request, is not guaranteed to be stable
    // across multiple invocations of `GetTree`.
    //
    // If part of the tree is missing from the CAS, the server will return the
    // portion present and omit the rest.
    //
    // Errors:
    //
    // * `NOT_FOUND`: The requested tree root is not present in the CAS.
    auto GetTree(::grpc::ServerContext* context,
                 const ::bazel_re::GetTreeRequest* request,
                 ::grpc::ServerWriter< ::bazel_re::GetTreeResponse>* writer)
        -> ::grpc::Status override;

  private:
    [[nodiscard]] auto CheckDigestConsistency(std::string const& ref,
                                              std::string const& computed)
        const noexcept -> std::optional<std::string>;

    gsl::not_null<Storage const*> storage_ = &Storage::Instance();
    Logger logger_{"execution-service"};
};
#endif
