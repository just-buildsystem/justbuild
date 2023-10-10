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

#include "src/buildtool/serve_api/remote/configuration_client.hpp"

#include <nlohmann/json.hpp>

#include "src/buildtool/execution_api/remote/config.hpp"

auto ConfigurationClient::CheckServeRemoteExecution() -> bool {
    auto client_remote_address = RemoteExecutionConfig::RemoteAddress();
    if (!client_remote_address) {
        logger_.Emit(LogLevel::Error,
                     "In order to use just-serve, also the "
                     "--remote-execution-address option must be given.");
        return false;
    }

    grpc::ClientContext context;
    justbuild::just_serve::RemoteExecutionEndpointRequest request{};
    justbuild::just_serve::RemoteExecutionEndpointResponse response{};
    grpc::Status status =
        stub_->RemoteExecutionEndpoint(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Error, status);
        return false;
    }
    auto serve_remote_endpoint = nlohmann::json::parse(response.address());
    if (serve_remote_endpoint != client_remote_address->ToJson()) {
        Logger::Log(LogLevel::Error,
                    "Different execution endpoint detected.\nIn order to "
                    "correctly use just serve, its remote execution endpoint "
                    "must be the same used by the client.\nserve remote "
                    "endpoint:  {}\nclient remote endpoint: {}",
                    serve_remote_endpoint.dump(),
                    client_remote_address->ToJson().dump());
        return false;
    }
    return true;
}
