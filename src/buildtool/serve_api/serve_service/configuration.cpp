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

#include "src/buildtool/serve_api/serve_service/configuration.hpp"

#include <optional>

#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"

auto ConfigurationService::RemoteExecutionEndpoint(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::RemoteExecutionEndpointRequest* /*request*/,
    ::justbuild::just_serve::RemoteExecutionEndpointResponse* response)
    -> ::grpc::Status {
    auto address = RemoteExecutionConfig::RemoteAddress();
    response->set_address(address ? address->ToJson().dump() : std::string());
    return ::grpc::Status::OK;
}

auto ConfigurationService::Compatibility(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::CompatibilityRequest* /*request*/,
    ::justbuild::just_serve::CompatibilityResponse* response)
    -> ::grpc::Status {
    response->set_compatible(Compatibility::IsCompatible());
    return ::grpc::Status::OK;
}
