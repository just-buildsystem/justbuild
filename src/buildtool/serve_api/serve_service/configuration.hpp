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

#ifndef INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_CONFIGURATION_HPP
#define INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_CONFIGURATION_HPP

#include <grpcpp/grpcpp.h>

#include "gsl/gsl"
#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "justbuild/just_serve/just_serve.pb.h"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"

// This service can be used by the client to double-check the server
// configuration.
class ConfigurationService final
    : public justbuild::just_serve::Configuration::Service {
  public:
    explicit ConfigurationService(
        HashFunction::Type hash_type,
        gsl::not_null<RemoteExecutionConfig const*> const&
            remote_config) noexcept
        : hash_type_{hash_type}, remote_config_{*remote_config} {};

    // Returns the address of the associated remote endpoint, if set,
    // or an empty string signaling that the serve endpoint acts also
    // as a remote execution endpoint.
    //
    // There are no method-specific errors.
    auto RemoteExecutionEndpoint(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::RemoteExecutionEndpointRequest* request,
        ::justbuild::just_serve::RemoteExecutionEndpointResponse* response)
        -> ::grpc::Status override;

    // Returns a flag signaling whether the associated remote-execution
    // endpoint uses the standard remote-execution protocol.
    //
    // There are no method-specific errors.
    auto Compatibility(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::CompatibilityRequest* request,
        ::justbuild::just_serve::CompatibilityResponse* response)
        -> ::grpc::Status override;

  private:
    HashFunction::Type hash_type_;
    RemoteExecutionConfig const& remote_config_;
};

#endif  // INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_CONFIGURATION_HPP
