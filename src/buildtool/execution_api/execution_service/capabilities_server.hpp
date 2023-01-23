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

#ifndef CAPABILITIES_SERVER_HPP
#define CAPABILITIES_SERVER_HPP

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"

class CapabilitiesServiceImpl final
    : public build::bazel::remote::execution::v2::Capabilities::Service {
  public:
    // GetCapabilities returns the server capabilities configuration of the
    // remote endpoint.
    // Only the capabilities of the services supported by the endpoint will
    // be returned:
    // * Execution + CAS + Action Cache endpoints should return both
    //   CacheCapabilities and ExecutionCapabilities.
    // * Execution only endpoints should return ExecutionCapabilities.
    // * CAS + Action Cache only endpoints should return CacheCapabilities.
    auto GetCapabilities(
        ::grpc::ServerContext* context,
        const ::build::bazel::remote::execution::v2::GetCapabilitiesRequest*
            request,
        ::build::bazel::remote::execution::v2::ServerCapabilities* response)
        -> ::grpc::Status override;
};
#endif
