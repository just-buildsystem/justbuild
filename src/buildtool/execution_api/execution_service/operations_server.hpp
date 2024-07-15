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

#ifndef OPERATIONS_SERVER_HPP
#define OPERATIONS_SERVER_HPP

#include "google/longrunning/operations.grpc.pb.h"
#include "gsl/gsl"
#include "src/buildtool/execution_api/execution_service/operation_cache.hpp"
#include "src/buildtool/logging/logger.hpp"

class OperarationsServiceImpl final
    : public ::google::longrunning::Operations::Service {
  public:
    explicit OperarationsServiceImpl(
        gsl::not_null<OperationCache const*> const& op_cache)
        : op_cache_{*op_cache} {};

    // Lists operations that match the specified filter in the request. If the
    // server doesn't support this method, it returns `UNIMPLEMENTED`.
    //
    // NOTE: the `name` binding below allows API services to override the
    // binding to use different resource name schemes, such as
    // `users/*/operations`.
    auto ListOperations(
        ::grpc::ServerContext* context,
        const ::google::longrunning::ListOperationsRequest* request,
        ::google::longrunning::ListOperationsResponse* response)
        -> ::grpc::Status override;
    // Gets the latest state of a long-running operation.  Clients can use this
    // method to poll the operation result at intervals as recommended by the
    // API service.
    auto GetOperation(::grpc::ServerContext* context,
                      const ::google::longrunning::GetOperationRequest* request,
                      ::google::longrunning::Operation* response)
        -> ::grpc::Status override;
    // Deletes a long-running operation. This method indicates that the client
    // is no longer interested in the operation result. It does not cancel the
    // operation. If the server doesn't support this method, it returns
    // `google.rpc.Code.UNIMPLEMENTED`.
    auto DeleteOperation(
        ::grpc::ServerContext* context,
        const ::google::longrunning::DeleteOperationRequest* request,
        ::google::protobuf::Empty* response) -> ::grpc::Status override;
    // Starts asynchronous cancellation on a long-running operation.  The server
    // makes a best effort to cancel the operation, but success is not
    // guaranteed.  If the server doesn't support this method, it returns
    // `google.rpc.Code.UNIMPLEMENTED`.  Clients can use
    // [Operations.GetOperation][google.longrunning.Operations.GetOperation] or
    // other methods to check whether the cancellation succeeded or whether the
    // operation completed despite cancellation. On successful cancellation,
    // the operation is not deleted; instead, it becomes an operation with
    // an [Operation.error][google.longrunning.Operation.error] value with a
    // [google.rpc.Status.code][google.rpc.Status.code] of 1, corresponding to
    // `Code.CANCELLED`.
    auto CancelOperation(
        ::grpc::ServerContext* context,
        const ::google::longrunning::CancelOperationRequest* request,
        ::google::protobuf::Empty* response) -> ::grpc::Status override;

  private:
    OperationCache const& op_cache_;
    Logger logger_{"execution-service:operations"};
};

#endif
