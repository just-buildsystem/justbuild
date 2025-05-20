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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/serve_api/serve_service/configuration.hpp"

#include <optional>
#include <string>

#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/logging/log_level.hpp"

auto ConfigurationService::RemoteExecutionEndpoint(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::RemoteExecutionEndpointRequest* /*request*/,
    ::justbuild::just_serve::RemoteExecutionEndpointResponse* response)
    -> ::grpc::Status {
    logger_->Emit(LogLevel::Debug, "RemoteExecutionEndpoint()");
    std::optional<ServerAddress> address;
    if (serve_config_.client_execution_address.has_value()) {
        address = serve_config_.client_execution_address;
    }
    else {
        address = remote_config_.remote_address;
    }
    response->set_address(address ? address->ToJson().dump() : std::string());
    return ::grpc::Status::OK;
}

auto ConfigurationService::Compatibility(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::CompatibilityRequest* /*request*/,
    ::justbuild::just_serve::CompatibilityResponse* response)
    -> ::grpc::Status {
    logger_->Emit(LogLevel::Debug, "Compatibility()");
    response->set_compatible(not ProtocolTraits::IsNative(hash_type_));
    return ::grpc::Status::OK;
}

#endif  // BOOTSTRAP_BUILD_TOOL
